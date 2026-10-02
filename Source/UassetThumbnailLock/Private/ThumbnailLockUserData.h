#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "UObject/SoftObjectPath.h"

#include "ThumbnailLockUserData.generated.h"

/**
 * Marker added to assets whose thumbnail is locked.
 * Unlike package metadata, asset user data is copied when the asset is duplicated,
 * which lets the duplicate inherit the lock and the locked thumbnail of its source.
 */
UCLASS()
class UThumbnailLockUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	/** Asset this marker was written for. When it differs from the owning asset, the owner is a duplicate of it. */
	UPROPERTY()
	FSoftObjectPath LockedAsset;

	virtual bool IsEditorOnly() const override { return true; }
};
