# Uasset Thumbnail Lock

Unreal Engine editor plugin to lock asset thumbnails so they are not regenerated or overwritten.

## Installation

1. Clone this repository into your project's `Plugins/` folder:
   ```
   git clone <repo-url> YourProject/Plugins/UassetThumbnailLock
   ```
2. Regenerate project files and build, or open the project and let the editor compile the plugin.
3. Enable **Uasset Thumbnail Lock** in *Edit > Plugins* if it is not enabled automatically.

## Layout

```
UassetThumbnailLock.uplugin
Source/UassetThumbnailLock/   Editor module (C++)
Content/                      Plugin content
Config/                       Plugin config
Resources/                    Icon128.png, etc.
```
