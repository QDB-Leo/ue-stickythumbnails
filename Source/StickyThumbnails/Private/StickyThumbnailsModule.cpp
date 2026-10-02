#include "StickyThumbnailsModule.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserMenuContexts.h"
#include "Editor.h"
#include "ImageUtils.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "LevelEditorViewport.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Styling/AppStyle.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "StickyThumbnailUserData.h"
#include "ToolMenus.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UnrealClient.h"

#define LOCTEXT_NAMESPACE "FStickyThumbnailsModule"

DEFINE_LOG_CATEGORY_STATIC(LogStickyThumbnails, Log, All);

const FName FStickyThumbnailsModule::LockedTag(TEXT("ThumbnailLocked"));

namespace StickyThumbnails
{
	static const TCHAR* LockedValue = TEXT("true");

	static UEditorAssetSubsystem* GetAssetSubsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	}

	static bool AreThumbnailsEqual(const FObjectThumbnail& A, const FObjectThumbnail& B)
	{
		return A.GetImageWidth() == B.GetImageWidth()
			&& A.GetImageHeight() == B.GetImageHeight()
			&& A.GetUncompressedImageData() == B.GetUncompressedImageData();
	}

	/** Same capture as Content Browser > Asset Actions > Capture Thumbnail: centered square of the active level viewport. */
	static bool CaptureActiveViewport(FObjectThumbnail& OutThumbnail)
	{
		FViewport* Viewport = GEditor ? GEditor->GetActiveViewport() : nullptr;
		if (!Viewport || !GCurrentLevelEditingViewportClient)
		{
			return false;
		}

		// Re-render without the viewport selection highlight
		FLevelEditorViewportClient* OldViewportClient = GCurrentLevelEditingViewportClient;
		GCurrentLevelEditingViewportClient = nullptr;
		Viewport->Draw();

		TArray<FColor> Pixels;
		const bool bReadPixels = Viewport->ReadPixels(Pixels);
		const FIntPoint ViewportSize = Viewport->GetSizeXY();

		GCurrentLevelEditingViewportClient = OldViewportClient;
		Viewport->Draw();

		if (!bReadPixels || ViewportSize.X <= 0 || ViewportSize.Y <= 0 || Pixels.Num() != ViewportSize.X * ViewportSize.Y)
		{
			return false;
		}

		const int32 CropSize = FMath::Min(ViewportSize.X, ViewportSize.Y);
		const int32 ScaledSize = FMath::Min<int32>(ThumbnailTools::DefaultThumbnailSize, CropSize);
		const int32 CropTop = (ViewportSize.Y - CropSize) / 2;
		const int32 CropLeft = (ViewportSize.X - CropSize) / 2;

		TArray<FColor> CroppedPixels;
		CroppedPixels.SetNumUninitialized(CropSize * CropSize);
		for (int32 Row = 0; Row < CropSize; ++Row)
		{
			FMemory::Memcpy(&CroppedPixels[Row * CropSize], &Pixels[(CropTop + Row) * ViewportSize.X + CropLeft], CropSize * sizeof(FColor));
		}

		TArray<FColor> ScaledPixels;
		if (ScaledSize < CropSize)
		{
			FImageUtils::ImageResize(CropSize, CropSize, CroppedPixels, ScaledSize, ScaledSize, ScaledPixels, true);
		}
		else
		{
			ScaledPixels = MoveTemp(CroppedPixels);
		}

		OutThumbnail.SetImageSize(ScaledSize, ScaledSize);
		TArray<uint8>& ImageData = OutThumbnail.AccessImageData();
		ImageData.SetNumUninitialized(ScaledPixels.Num() * sizeof(FColor));
		FMemory::Memcpy(ImageData.GetData(), ScaledPixels.GetData(), ImageData.Num());
		return true;
	}
}

void FStickyThumbnailsModule::StartupModule()
{
	// Expose the metadata as an asset registry tag so the lock state is known without loading the asset
	UObject::GetMetaDataTagsForAssetRegistry().Add(LockedTag);

	PreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this, &FStickyThumbnailsModule::HandleObjectPreSave);
	PackageSavedHandle = UPackage::PackageSavedWithContextEvent.AddRaw(this, &FStickyThumbnailsModule::HandlePackageSaved);
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FStickyThumbnailsModule::HandleObjectPropertyChanged);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	AssetCreatedHandle = AssetRegistry.OnInMemoryAssetCreated().AddRaw(this, &FStickyThumbnailsModule::HandleInMemoryAssetCreated);
	AssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddRaw(this, &FStickyThumbnailsModule::HandleAssetRenamed);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FStickyThumbnailsModule::RegisterMenus));
}

void FStickyThumbnailsModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
	{
		AssetRegistry->OnInMemoryAssetCreated().Remove(AssetCreatedHandle);
		AssetRegistry->OnAssetRenamed().Remove(AssetRenamedHandle);
	}

	FCoreUObjectDelegates::OnObjectPreSave.Remove(PreSaveHandle);
	UPackage::PackageSavedWithContextEvent.Remove(PackageSavedHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);

	if (DeferredWorkHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredWorkHandle);
		DeferredWorkHandle.Reset();
	}

	UObject::GetMetaDataTagsForAssetRegistry().Remove(LockedTag);

	LockedThumbnails.Empty();
}

bool FStickyThumbnailsModule::SupportsThumbnailLock(const UClass* Class)
{
	return Class && Class->IsChildOf(ULevelSequence::StaticClass());
}

bool FStickyThumbnailsModule::IsThumbnailLocked(UObject* Object)
{
	UEditorAssetSubsystem* AssetSubsystem = StickyThumbnails::GetAssetSubsystem();
	return Object && AssetSubsystem && AssetSubsystem->GetMetadataTag(Object, LockedTag) == StickyThumbnails::LockedValue;
}

bool FStickyThumbnailsModule::IsThumbnailLocked(const FAssetData& AssetData)
{
	// The registry tag is only refreshed on save, so prefer the in-memory metadata when the asset is loaded
	if (UObject* LoadedAsset = AssetData.FastGetAsset(false))
	{
		return IsThumbnailLocked(LoadedAsset);
	}

	FString Value;
	return AssetData.GetTagValue(LockedTag, Value) && Value == StickyThumbnails::LockedValue;
}

void FStickyThumbnailsModule::SetThumbnailLocked(UObject* Object, bool bLocked)
{
	UEditorAssetSubsystem* AssetSubsystem = StickyThumbnails::GetAssetSubsystem();
	if (!Object || !AssetSubsystem || !SupportsThumbnailLock(Object->GetClass()) || IsThumbnailLocked(Object) == bLocked)
	{
		return;
	}

	if (bLocked)
	{
		AssetSubsystem->SetMetadataTag(Object, LockedTag, StickyThumbnails::LockedValue);

		// Keep the thumbnail currently in memory; if there is none, the one on disk is used at save time
		AdoptCachedThumbnail(Object);
	}
	else
	{
		AssetSubsystem->RemoveMetadataTag(Object, LockedTag);
		LockedThumbnails.Remove(FObjectKey(Object));
	}

	SetLockUserData(Object, bLocked);
	Object->MarkPackageDirty();

	UE_LOG(LogStickyThumbnails, Log, TEXT("%s thumbnail of %s"), bLocked ? TEXT("Locked") : TEXT("Unlocked"), *Object->GetPathName());
}

bool FStickyThumbnailsModule::CaptureAndLockThumbnails(const TArray<UObject*>& Objects)
{
	FObjectThumbnail CapturedThumbnail;
	if (!StickyThumbnails::CaptureActiveViewport(CapturedThumbnail))
	{
		UE_LOG(LogStickyThumbnails, Warning, TEXT("Could not capture the active level viewport"));
		return false;
	}

	for (UObject* Object : Objects)
	{
		if (!Object || !SupportsThumbnailLock(Object->GetClass()))
		{
			continue;
		}

		UPackage* Package = Object->GetOutermost();
		if (FObjectThumbnail* NewThumbnail = ThumbnailTools::CacheThumbnail(Object->GetFullName(), &CapturedThumbnail, Package))
		{
			NewThumbnail->MarkAsDirty();
			NewThumbnail->SetCreatedAfterCustomThumbsEnabled();
		}
		Package->MarkPackageDirty();

		if (IsThumbnailLocked(Object))
		{
			AdoptCachedThumbnail(Object);
			SetLockUserData(Object, true);
		}
		else
		{
			SetThumbnailLocked(Object, true);
		}

		// Lets thumbnail pools pick up the new image
		Object->PostEditChange();
	}

	return true;
}

void FStickyThumbnailsModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Level Sequence actions section, at the top of the context menu.
	// Asset context menus are hierarchical, so this also covers ULevelSequence subclasses.
	// Unlocking is done with Asset Actions > Clear Thumbnail (see ProcessDeferredWork).
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.LevelSequence");
	FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
	Section.AddDynamicEntry("StickyThumbnails", FNewToolMenuSectionDelegate::CreateRaw(this, &FStickyThumbnailsModule::PopulateAssetContextMenu));
}

void FStickyThumbnailsModule::PopulateAssetContextMenu(FToolMenuSection& Section)
{
	const UContentBrowserAssetContextMenuContext* Context = Section.FindContext<UContentBrowserAssetContextMenuContext>();
	if (!Context)
	{
		return;
	}

	TArray<FAssetData> Assets;
	for (const FAssetData& AssetData : Context->SelectedAssets)
	{
		if (SupportsThumbnailLock(AssetData.GetClass()))
		{
			Assets.Add(AssetData);
		}
	}

	if (Assets.Num() == 0)
	{
		return;
	}

	Section.AddMenuEntry(
		"CaptureAndLockThumbnail",
		LOCTEXT("CaptureAndLockThumbnail", "Capture & Lock Thumbnail"),
		LOCTEXT("CaptureAndLockThumbnailTooltip", "Capture the active level viewport as the thumbnail and lock it: it will no longer be refreshed when the asset is saved."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Lock"),
		FUIAction(
			FExecuteAction::CreateLambda([this, Assets]()
			{
				TArray<UObject*> Objects;
				for (const FAssetData& AssetData : Assets)
				{
					Objects.Add(AssetData.GetAsset());
				}
				CaptureAndLockThumbnails(Objects);
			}),
			FCanExecuteAction::CreateLambda([]() { return GEditor && GEditor->GetActiveViewport() && GCurrentLevelEditingViewportClient; })));
}

FObjectThumbnail* FStickyThumbnailsModule::FindLockedThumbnail(UObject* Object)
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

bool FStickyThumbnailsModule::AdoptCachedThumbnail(UObject* Object)
{
	const FObjectThumbnail* CachedThumbnail = ThumbnailTools::FindCachedThumbnail(Object->GetFullName());
	if (!CachedThumbnail || CachedThumbnail->IsEmpty())
	{
		return false;
	}

	const FObjectThumbnail* LockedThumbnail = FindLockedThumbnail(Object);
	if (LockedThumbnail && StickyThumbnails::AreThumbnailsEqual(*LockedThumbnail, *CachedThumbnail))
	{
		return false;
	}

	LockedThumbnails.Add(FObjectKey(Object), *CachedThumbnail);
	return true;
}

void FStickyThumbnailsModule::RestoreLockedThumbnail(UObject* Object)
{
	if (FObjectThumbnail* LockedThumbnail = FindLockedThumbnail(Object))
	{
		ThumbnailTools::CacheThumbnail(Object->GetFullName(), LockedThumbnail, Object->GetOutermost());

		// Thumbnail pools refresh an asset's thumbnail when it reports a property change
		FPropertyChangedEvent EmptyPropertyChangedEvent(nullptr);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Object, EmptyPropertyChangedEvent);
	}
}

UStickyThumbnailUserData* FStickyThumbnailsModule::FindLockUserData(UObject* Object)
{
	IInterface_AssetUserData* UserDataOwner = Cast<IInterface_AssetUserData>(Object);
	return UserDataOwner ? Cast<UStickyThumbnailUserData>(UserDataOwner->GetAssetUserDataOfClass(UStickyThumbnailUserData::StaticClass())) : nullptr;
}

void FStickyThumbnailsModule::SetLockUserData(UObject* Object, bool bLocked)
{
	IInterface_AssetUserData* UserDataOwner = Cast<IInterface_AssetUserData>(Object);
	if (!UserDataOwner)
	{
		return;
	}

	if (!bLocked)
	{
		UserDataOwner->RemoveUserDataOfClass(UStickyThumbnailUserData::StaticClass());
		return;
	}

	UStickyThumbnailUserData* UserData = FindLockUserData(Object);
	if (!UserData)
	{
		UserData = NewObject<UStickyThumbnailUserData>(Object);
		UserDataOwner->AddAssetUserData(UserData);
	}
	UserData->LockedAsset = FSoftObjectPath(Object);
}

void FStickyThumbnailsModule::CopyLockFromDuplicateSource(UObject* Object)
{
	UStickyThumbnailUserData* UserData = FindLockUserData(Object);
	if (!UserData || UserData->LockedAsset == FSoftObjectPath(Object))
	{
		return;
	}

	// The lock marker was copied from another asset: this one is a duplicate of it
	UObject* Source = UserData->LockedAsset.ResolveObject();
	UserData->LockedAsset = FSoftObjectPath(Object);

	if (UEditorAssetSubsystem* AssetSubsystem = StickyThumbnails::GetAssetSubsystem())
	{
		AssetSubsystem->SetMetadataTag(Object, LockedTag, StickyThumbnails::LockedValue);
	}

	if (Source && Source != Object)
	{
		if (const FObjectThumbnail* SourceThumbnail = FindLockedThumbnail(Source))
		{
			const FObjectThumbnail ThumbnailCopy = *SourceThumbnail;
			LockedThumbnails.Add(FObjectKey(Object), ThumbnailCopy);
			RestoreLockedThumbnail(Object);
		}
	}

	Object->MarkPackageDirty();

	UE_LOG(LogStickyThumbnails, Log, TEXT("Locked thumbnail of duplicate %s"), *Object->GetPathName());
}

void FStickyThumbnailsModule::HandleObjectPreSave(UObject* Object, FObjectPreSaveContext SaveContext)
{
	if (SaveContext.IsProceduralSave() || !Object || !Object->IsAsset() || !SupportsThumbnailLock(Object->GetClass()) || !IsThumbnailLocked(Object))
	{
		return;
	}

	// Cleared thumbnail (Clear Thumbnail): the asset gets unlocked, keep it cleared
	const FObjectThumbnail* CachedThumbnail = ThumbnailTools::FindCachedThumbnail(Object->GetFullName());
	if (CachedThumbnail && CachedThumbnail->IsEmpty())
	{
		return;
	}

	// Runs after the editor / Sequencer have generated the new thumbnail: put the locked one back before it gets written
	if (FObjectThumbnail* LockedThumbnail = FindLockedThumbnail(Object))
	{
		ThumbnailTools::CacheThumbnail(Object->GetFullName(), LockedThumbnail, Object->GetOutermost());
	}
}

void FStickyThumbnailsModule::HandlePackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext SaveContext)
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

	// A thumbnail may also be captured right after the save; restore the locked one next tick
	PendingRestores.Add(Asset);
	ScheduleDeferredWork();
}

void FStickyThumbnailsModule::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	// Capture / Clear Thumbnail cache the new (or empty) image then call PostEditChange on the asset: check for it next tick
	if (Object && Object->IsAsset() && SupportsThumbnailLock(Object->GetClass()))
	{
		PendingCaptureChecks.Add(Object);
		ScheduleDeferredWork();
	}
}

void FStickyThumbnailsModule::HandleInMemoryAssetCreated(UObject* Object)
{
	// Duplicates are reported here; their sub-objects are only final once duplication completes
	if (Object && SupportsThumbnailLock(Object->GetClass()))
	{
		PendingDuplicates.Add(Object);
		ScheduleDeferredWork();
	}
}

void FStickyThumbnailsModule::HandleAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath)
{
	if (UObject* Asset = AssetData.FastGetAsset(false))
	{
		if (UStickyThumbnailUserData* UserData = FindLockUserData(Asset))
		{
			// Keep the marker pointing at its owner, otherwise the asset would be taken for a duplicate
			UserData->LockedAsset = FSoftObjectPath(Asset);
		}
	}
}

void FStickyThumbnailsModule::ScheduleDeferredWork()
{
	if (!DeferredWorkHandle.IsValid())
	{
		DeferredWorkHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FStickyThumbnailsModule::ProcessDeferredWork));
	}
}

bool FStickyThumbnailsModule::ProcessDeferredWork(float DeltaTime)
{
	DeferredWorkHandle.Reset();

	TSet<TWeakObjectPtr<UObject>> Restores = MoveTemp(PendingRestores);
	TSet<TWeakObjectPtr<UObject>> Duplicates = MoveTemp(PendingDuplicates);
	TSet<TWeakObjectPtr<UObject>> CaptureChecks = MoveTemp(PendingCaptureChecks);

	for (const TWeakObjectPtr<UObject>& WeakObject : Duplicates)
	{
		if (UObject* Object = WeakObject.Get())
		{
			CopyLockFromDuplicateSource(Object);
		}
	}

	for (const TWeakObjectPtr<UObject>& WeakObject : Restores)
	{
		// A thumbnail that changes around a save is the automatic refresh, never a manual capture
		CaptureChecks.Remove(WeakObject);

		UObject* Object = WeakObject.Get();
		if (Object && IsThumbnailLocked(Object))
		{
			RestoreLockedThumbnail(Object);
		}
	}

	for (const TWeakObjectPtr<UObject>& WeakObject : CaptureChecks)
	{
		UObject* Object = WeakObject.Get();
		if (!Object || !IsThumbnailLocked(Object))
		{
			continue;
		}

		const FObjectThumbnail* CachedThumbnail = ThumbnailTools::FindCachedThumbnail(Object->GetFullName());
		if (CachedThumbnail && CachedThumbnail->IsEmpty())
		{
			// Clear Thumbnail unlocks the asset
			SetThumbnailLocked(Object, false);
		}
		else if (AdoptCachedThumbnail(Object))
		{
			SetLockUserData(Object, true);
			UE_LOG(LogStickyThumbnails, Log, TEXT("Captured thumbnail kept as the locked thumbnail of %s"), *Object->GetPathName());
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FStickyThumbnailsModule, StickyThumbnails)
