# Uasset Thumbnail Lock

Unreal Engine editor plugin to lock asset thumbnails so they are not regenerated or overwritten.

## Installation

1. Clone this repository into your project's `Plugins/` folder:
   ```
   git clone <repo-url> YourProject/Plugins/UassetThumbnailLock
   ```
2. Regenerate project files and build, or open the project and let the editor compile the plugin.
3. Enable **Uasset Thumbnail Lock** in *Edit > Plugins* if it is not enabled automatically.

## Usage

Right-click a **Level Sequence** (Cinematic Assemblies included) in the Content Browser:

- **Capture & Lock Thumbnail** (Level Sequence actions, top of the menu): captures the active level viewport as the thumbnail and locks it. Saving the asset no longer refreshes it.
- **Asset Actions > Capture Thumbnail** (engine): on a locked asset, the captured image becomes the locked thumbnail.
- **Asset Actions > Clear Thumbnail** (engine): clears the thumbnail and unlocks the asset.

Multi-selection is supported. Duplicating a locked asset gives a locked duplicate with the same thumbnail.

### How it works

- The lock is stored as package metadata (`ThumbnailLocked=true`) on the asset, so it is shared through source control. Toggling it marks the asset dirty: save it to persist the lock.
- The tag is also exposed to the asset registry, so the menu knows the state without loading the asset.
- When a locked asset is saved, the thumbnail being written is replaced by the locked one (captured at lock time or manually, or read back from the saved `.uasset`).
- A thumbnail change outside of a save (manual capture) is adopted as the new locked thumbnail; a cleared thumbnail unlocks the asset.
- Locked assets also carry an editor-only asset user data marker (`UThumbnailLockUserData`). It is copied on duplication, which is how a duplicate finds its source and inherits the lock.

## Layout

```
UassetThumbnailLock.uplugin
Source/UassetThumbnailLock/   Editor module (C++)
Content/                      Plugin content
Config/                       Plugin config
Resources/                    Icon128.png, etc.
```
