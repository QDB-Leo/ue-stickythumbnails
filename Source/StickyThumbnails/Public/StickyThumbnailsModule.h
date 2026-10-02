#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Misc/ObjectThumbnail.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectKey.h"

class FObjectPostSaveContext;
class FObjectPreSaveContext;
class UStickyThumbnailUserData;
struct FAssetData;
struct FPropertyChangedEvent;
struct FToolMenuSection;

/**
 * Prevents the thumbnail of locked assets from being regenerated when they are saved.
 *
 * The lock is stored as package metadata on the asset itself (so it is shared through source control)
 * and exposed as an asset registry tag so the Content Browser can read it without loading the asset.
 * While an asset is locked, the thumbnail written on save is replaced by the one it had when it was locked,
 * or by the last one captured manually (Capture Thumbnail). Clear Thumbnail unlocks the asset.
 */
class FStickyThumbnailsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Metadata / asset registry tag set on assets whose thumbnail is locked. */
	static const FName LockedTag;

	/** Only Level Sequences (and subclasses, e.g. Cinematic Assemblies) are supported for now. */
	static bool SupportsThumbnailLock(const UClass* Class);

	static bool IsThumbnailLocked(UObject* Object);
	static bool IsThumbnailLocked(const FAssetData& AssetData);

	void SetThumbnailLocked(UObject* Object, bool bLocked);

	/** Captures the active level viewport as the thumbnail of the given assets, then locks them. */
	bool CaptureAndLockThumbnails(const TArray<UObject*>& Objects);

private:
	void RegisterMenus();
	void PopulateAssetContextMenu(FToolMenuSection& Section);

	void HandleObjectPreSave(UObject* Object, FObjectPreSaveContext SaveContext);
	void HandlePackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext SaveContext);
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
	void HandleInMemoryAssetCreated(UObject* Object);
	void HandleAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);

	/** Returns the thumbnail to keep for a locked asset, loading it from the saved package if needed. */
	FObjectThumbnail* FindLockedThumbnail(UObject* Object);

	/** Makes the thumbnail currently cached in memory the locked one. Returns false if there is none or it is unchanged. */
	bool AdoptCachedThumbnail(UObject* Object);

	/** Puts the locked thumbnail back in memory and refreshes the Content Browser. */
	void RestoreLockedThumbnail(UObject* Object);

	void CopyLockFromDuplicateSource(UObject* Object);
	static UStickyThumbnailUserData* FindLockUserData(UObject* Object);
	static void SetLockUserData(UObject* Object, bool bLocked);

	void ScheduleDeferredWork();
	bool ProcessDeferredWork(float DeltaTime);

	/** Thumbnails kept for locked assets (captured at lock time, captured manually, or loaded from disk). */
	TMap<FObjectKey, FObjectThumbnail> LockedThumbnails;

	/** Work done on the next tick, once saves / duplications / captures have completed. */
	TSet<TWeakObjectPtr<UObject>> PendingRestores;
	TSet<TWeakObjectPtr<UObject>> PendingDuplicates;
	TSet<TWeakObjectPtr<UObject>> PendingCaptureChecks;
	FTSTicker::FDelegateHandle DeferredWorkHandle;

	FDelegateHandle PreSaveHandle;
	FDelegateHandle PackageSavedHandle;
	FDelegateHandle PropertyChangedHandle;
	FDelegateHandle AssetCreatedHandle;
	FDelegateHandle AssetRenamedHandle;
};
