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

- **Lock Thumbnail**: keeps the current thumbnail. Saving the asset no longer refreshes it.
- **Unlock Thumbnail**: the thumbnail is refreshed on save again.

Multi-selection is supported. To change a locked thumbnail: unlock, save (or capture a new thumbnail), then lock again.

### How it works

- The lock is stored as package metadata (`ThumbnailLocked=true`) on the asset, so it is shared through source control. Toggling it marks the asset dirty: save it to persist the lock.
- The tag is also exposed to the asset registry, so the menu knows the state without loading the asset.
- When a locked asset is saved, the thumbnail being written is replaced by the locked one (captured at lock time, or read back from the saved `.uasset`).

## Layout

```
UassetThumbnailLock.uplugin
Source/UassetThumbnailLock/   Editor module (C++)
Content/                      Plugin content
Config/                       Plugin config
Resources/                    Icon128.png, etc.
```
