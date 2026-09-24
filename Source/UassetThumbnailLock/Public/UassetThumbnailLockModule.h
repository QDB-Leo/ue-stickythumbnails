#pragma once

#include "CoreMinimal.h"
#include "Misc/ObjectThumbnail.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectKey.h"

class FObjectPostSaveContext;
class FObjectPreSaveContext;
class UToolMenu;
struct FAssetData;
struct FToolMenuSection;

/**
 * Prevents the thumbnail of locked assets from being regenerated when they are saved.
 *
 * The lock is stored as package metadata on the asset itself (so it is shared through source control)
 * and exposed as an asset registry tag so the Content Browser can read it without loading the asset.
 * While an asset is locked, the thumbnail written on save is replaced by the one it had when it was locked.
 */
class FUassetThumbnailLockModule : public IModuleInterface
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

private:
	void RegisterMenus();
	void PopulateAssetContextMenu(FToolMenuSection& Section);
	void SetThumbnailLocked(const TArray<FAssetData>& Assets, bool bLocked);

	void HandleObjectPreSave(UObject* Object, FObjectPreSaveContext SaveContext);
	void HandlePackageSaved(const FString& PackageFilename, UPackage* Package, FObjectPostSaveContext SaveContext);

	/** Returns the thumbnail to keep for a locked asset, loading it from the saved package if needed. */
	FObjectThumbnail* FindLockedThumbnail(UObject* Object);

	/** Thumbnails captured when assets were locked (or loaded from disk on first save this session). */
	TMap<FObjectKey, FObjectThumbnail> LockedThumbnails;

	FDelegateHandle PreSaveHandle;
	FDelegateHandle PackageSavedHandle;
};
