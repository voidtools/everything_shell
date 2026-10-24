# EverythingShell

A Windows 11 modern context menu item for [Everything](https://www.voidtools.com).

Right-click a folder, or the background of a folder, and choose **Search Everything...** to open Everything with that location:

```
Everything.exe -path "<folder>"
```

The title is a string resource and follows the Windows display language.

On This PC, which is not a file system folder, it opens Everything with an empty search (`-search ""`).

## Download

https://github.com/voidtools/everything_shell/releases/

## How it works

- `EverythingShell.dll` is a small COM server implementing `IExplorerCommand` (and `IObjectWithSite`, to find the current folder when the background is clicked). It is written in C and built without the C runtime.
- The Windows 11 context menu only loads handlers registered by a package with identity, so the DLL is registered by an MSIX package (`windows.fileExplorerContextMenus` + `windows.comServer` surrogate server).
- Folders and the folder background use `desktop5:ItemType` (`Directory`, `Directory\Background`). Drives use `desktop10:ItemType` (`Drive`), which needs Windows 11 22H2 (build 22621) or later.
- `Everything.exe` is found in the `ExePath` value of `HKEY_LOCAL_MACHINE\SOFTWARE\voidtools\Everything`. If that does not exist, `C:\Program Files\Everything\Everything.exe` is used.
- `EverythingShellStub.exe` is a placeholder. The package manifest requires an application with an executable; it does nothing and is never run.

## Requirements

- Windows 11
- Visual Studio 2026 (C++ desktop workload) and the Windows SDK, for `makeappx.exe`
- A code signing certificate. An MSIX package must be signed to install.

## Layout

```
vs2026\                  EverythingShell.sln, EverythingShell.vcxproj, EverythingShellStub.vcxproj
EverythingShell\src\     DLL source, version.h (dll / exe version)
EverythingShell\res\     Everything.ico, string tables, version info, .def
EverythingShellStub\     placeholder exe
Package\                 AppxManifest.xml, Build-Package.ps1
Changes.txt              change log
```

## Build

1. Open `vs2026\EverythingShell.sln` and build **x64** (Debug or Release). Output goes to `vs2026\bin\x64\<Configuration>`.
2. Code sign `EverythingShell.dll` and `EverythingShellStub.exe`.
3. Run `Package\Build-Package.ps1 [-Configuration Debug|Release]`. This creates `EverythingShell.msix` (unsigned) next to the binaries. The logos are generated from `res\Everything.ico`.
4. Sign `EverythingShell.msix`.

`Publisher` in `Package\AppxManifest.xml` must be exactly the Subject of the certificate that signs the package. Replace it with your own if you build this yourself.

## Install

```
Add-AppxPackage .\EverythingShell.msix
```

The signing certificate must be trusted on the machine. You may need to restart Explorer before the item appears.

## Uninstall

```
Get-AppxPackage voidtools.EverythingShell | Remove-AppxPackage
```

## Translations

Add a `STRINGTABLE` block for the language to `EverythingShell\res\EverythingShell.rc`, using the same `IDS_SEARCH_EVERYTHING` id as the existing blocks.

## Forking

Generate your own CLSID (`uuidgen`) and replace it in `EverythingShell\src\EverythingShell.c` (`DEFINE_GUID`) and in `Package\AppxManifest.xml` (the `Clsid` and `Class Id` attributes).

## Version

The dll and exe version is set in `EverythingShell\src\version.h`. The `Identity` `Version` in `Package\AppxManifest.xml` and `Changes.txt` are updated by hand.

## License

MIT, see [LICENSE](LICENSE).

## See also
https://www.voidtools.com/forum/viewtopic.php?t=18009
