#include "UassetThumbnailLockModule.h"

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserMenuContexts.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Styling/AppStyle.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "ToolMenus.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "FUassetThumbnailLockModule"

DEFINE_LOG_CATEGORY_STATIC(LogUassetThumbnailLock, Log, All);

const FName FUassetThumbnailLockModule::LockedTag(TEXT("ThumbnailLocked"));

namespace UassetThumbnailLock
{
	static const TCHAR* LockedValue = TEXT("true");

	static UEditorAssetSubsystem* GetAssetSubsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	}
}

void FUassetThumbnailLockModule::StartupModule()
{
	// Expose the metadata as an asset registry tag so the lock state is known without loading the asset
	UObject::GetMetaDataTagsForAssetRegistry().Add(LockedTag);

	PreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this, &FUassetThumbnailLockModule::HandleObjectPreSave);
	PackageSavedHandle = UPackage::PackageSavedWithContextEvent.AddRaw(this, &FUassetThumbnailLockModule::HandlePackageSaved);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUassetThumbnailLockModule::RegisterMenus));
}

void FUassetThumbnailLockModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FCoreUObjectDelegates::OnObjectPreSave.Remove(PreSaveHandle);
	UPackage::PackageSavedWithContextEvent.Remove(PackageSavedHandle);

	UObject::GetMetaDataTagsForAssetRegistry().Remove(LockedTag);

	LockedThumbnails.Empty();
}

bool FUassetThumbnailLockModule::SupportsThumbnailLock(const UClass* Class)
{
	return Class && Class->IsChildOf(ULevelSequence::StaticClass());
}

bool FUassetThumbnailLockModule::IsThumbnailLocked(UObject* Object)
{
	UEditorAssetSubsystem* AssetSubsystem = UassetThumbnailLock::GetAssetSubsystem();
	return Object && AssetSubsystem && AssetSubsystem->GetMetadataTag(Object, LockedTag) == UassetThumbnailLock::LockedValue;
}

bool FUassetThumbnailLockModule::IsThumbnailLocked(const FAssetData& AssetData)
{
	// The registry tag is only refreshed on save, so prefer the in-memory metadata when the asset is loaded
	if (UObject* LoadedAsset = AssetData.FastGetAsset(false))
	{
		return IsThumbnailLocked(LoadedAsset);
	}

	FString Value;
	return AssetData.GetTagValue(LockedTag, Value) && Value == UassetThumbnailLock::LockedValue;
}

void FUassetThumbnailLockModule::SetThumbnailLocked(UObject* Object, bool bLocked)
{
	UEditorAssetSubsystem* AssetSubsystem = UassetThumbnailLock::GetAssetSubsystem();
	if (!Object || !AssetSubsystem || !SupportsThumbnailLock(Object->GetClass()) || IsThumbnailLocked(Object) == bLocked)
	{
		return;
	}

	const FObjectKey Key(Object);
	if (bLocked)
	{
		AssetSubsystem->SetMetadataTag(Object, LockedTag, UassetThumbnailLock::LockedValue);

		// Keep the thumbnail currently in memory; if there is none, the one on disk is used at save time
		if (const FObjectThumbnail* CurrentThumbnail = ThumbnailTools::FindCachedThumbnail(Object->GetFullName()))
		{
			if (!CurrentThumbnail->IsEmpty())
			{
				LockedThumbnails.Add(Key, *CurrentThumbnail);
			}
		}
	}
	else
	{
		AssetSubsystem->RemoveMetadataTag(Object, LockedTag);
		LockedThumbnails.Remove(Key);
	}

	Object->MarkPackageDirty();

	UE_LOG(LogUassetThumbnailLock, Log, TEXT("%s thumbnail of %s"), bLocked ? TEXT("Locked") : TEXT("Unlocked"), *Object->GetPathName());
}

void FUassetThumbnailLockModule::SetThumbnailLocked(const TArray<FAssetData>& Assets, bool bLocked)
{
	for (const FAssetData& AssetData : Assets)
	{
		SetThumbnailLocked(AssetData.GetAsset(), bLocked);
	}
}

void FUassetThumbnailLockModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Asset context menus are hierarchical, so this also covers ULevelSequence subclasses
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.LevelSequence");
	FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
	Section.AddDynamicEntry("UassetThumbnailLock", FNewToolMenuSectionDelegate::CreateRaw(this, &FUassetThumbnailLockModule::PopulateAssetContextMenu));
}

void FUassetThumbnailLockModule::PopulateAssetContextMenu(FToolMenuSection& Section)
{
	const UContentBrowserAssetContextMenuContext* Context = Section.FindContext<UContentBrowserAssetContextMenuContext>();
	if (!Context)
	{
		return;
	}

	TArray<FAssetData> ToLock;
	TArray<FAssetData> ToUnlock;
	for (const FAssetData& AssetData : Context->SelectedAssets)
	{
		if (SupportsThumbnailLock(AssetData.GetClass()))
		{
			(IsThumbnailLocked(AssetData) ? ToUnlock : ToLock).Add(AssetData);
		}
	}

	if (ToLock.Num() > 0)
	{
		Section.AddMenuEntry(
			"LockThumbnail",
			LOCTEXT("LockThumbnail", "Lock Thumbnail"),
			LOCTEXT("LockThumbnailTooltip", "Keep the current thumbnail: it will no longer be refreshed when the asset is saved."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Lock"),
			FUIAction(FExecuteAction::CreateLambda([this, ToLock]() { SetThumbnailLocked(ToLock, true); })));
	}

	if (ToUnlock.Num() > 0)
	{
		Section.AddMenuEntry(
			"UnlockThumbnail",
			LOCTEXT("UnlockThumbnail", "Unlock Thumbnail"),
			LOCTEXT("UnlockThumbnailTooltip", "Let the thumbnail be refreshed again when the asset is saved."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Unlock"),
			FUIAction(FExecuteAction::CreateLambda([this, ToUnlock]() { SetThumbnailLocked(ToUnlock, false); })));
	}
}

FObjectThumbnail* FUassetThumbnailLockModule::FindLockedThumbnail(UObject* Object)
{
	const FObjectKey Key(Object);
	if (FObjectThumbnail* LockedThumbnail = LockedThumbnails.Find(Key))
	{
		return LockedThumbnail;
	}

	// Not captured this session: the thumbnail saved on disk is the locked one
	FString PackageFilename;
	if (!FPackageName::DoesPackageExist(Object->GetOutermost()->GetName(), &PackageFilename))
	{
		return nullptr;
	}

	const FName ObjectFullName(*Object->GetFullName());
	FThumbnailMap DiskThumbnails;
	if (!ThumbnailTools::LoadThumbnailsFromPackage(PackageFilename, TSet<FName>{ ObjectFullName }, DiskThumbnails))
	{
		return nullptr;
	}

	const FObjectThumbnail* DiskThumbnail = DiskThumbnails.Find(ObjectFullName);
	if (!DiskThumbnail || DiskThumbnail->IsEmpty())
	{
		return nullptr;
	}

	return &LockedThumbnails.Add(Key, *DiskThumbnail);
}

void FUassetThumbnailLockModule::HandleObjectPreSave(UObject* Object, FObjectPreSaveContext SaveContext)
{
	if (SaveContext.IsProceduralSave() || !Object || !Object->IsAsset() || !SupportsThumbnailLock(Object->GetClass()) || !IsThumbnailLocked(Object))
	{
		return;
	}

	// Runs after the editor / Sequencer have generated the new thumbnail: put the locked one back before it gets written
	if (FObjectThumbnail* LockedThumbnail = FindLockedThumbnail(Object))
	{
		ThumbnailTools::CacheThumbnail(Object->GetFullName(), LockedThumbnail, Object->GetOutermost());
	}
}

void FUassetThumbnailLockModule::HandlePackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext SaveContext)
{
	if (SaveContext.IsProceduralSave() || !Package)
	{
		return;
	}

	UObject* Asset = Package->FindAssetInPackage();
	if (!Asset || !SupportsThumbnailLock(Asset->GetClass()) || !IsThumbnailLocked(Asset))
	{
		return;
	}

	// A thumbnail may also be captured right after the save; restore the locked one next tick and refresh the Content Browser
	TWeakObjectPtr<UObject> WeakAsset(Asset);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, WeakAsset](float)
	{
		UObject* LockedAsset = WeakAsset.Get();
		if (LockedAsset && IsThumbnailLocked(LockedAsset))
		{
			if (FObjectThumbnail* LockedThumbnail = FindLockedThumbnail(LockedAsset))
			{
				ThumbnailTools::CacheThumbnail(LockedAsset->GetFullName(), LockedThumbnail, LockedAsset->GetOutermost());

				// Thumbnail pools refresh an asset's thumbnail when it reports a property change
				FPropertyChangedEvent EmptyPropertyChangedEvent(nullptr);
				FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(LockedAsset, EmptyPropertyChangedEvent);
			}
		}
		return false;
	}));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUassetThumbnailLockModule, UassetThumbnailLock)
