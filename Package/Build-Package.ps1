#
# Copyright (C) 2026 voidtools / David Carpenter
# 
# Permission is hereby granted, free of charge, 
# to any person obtaining a copy of this software 
# and associated documentation files (the "Software"), 
# to deal in the Software without restriction, 
# including without limitation the rights to use, 
# copy, modify, merge, publish, distribute, sublicense, 
# and/or sell copies of the Software, and to permit 
# persons to whom the Software is furnished to do so, 
# subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be 
# included in all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
# OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#

# Packs bin\x64\<Configuration>\EverythingShell.dll and EverythingShellStub.exe
# into bin\x64\<Configuration>\EverythingShell.msix. The msix is NOT signed.
#
# 1. build the solution (x64)
# 2. code sign EverythingShell.dll and EverythingShellStub.exe in bin\x64\<Configuration>
# 3. run this script
# 4. sign EverythingShell.msix; the cert Subject must exactly match Publisher in AppxManifest.xml
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'

$root  = Split-Path $PSScriptRoot
$bin   = Join-Path $root "vs2026\bin\x64\$Configuration"
$stage = Join-Path $root "vs2026\obj\Package\$Configuration"
$msix  = Join-Path $bin 'EverythingShell.msix'

foreach ($f in 'EverythingShell.dll', 'EverythingShellStub.exe') {
	if (!(Test-Path (Join-Path $bin $f))) { throw "$f not found in $bin - build the solution first." }
}

$makeappx = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.*\x64\makeappx.exe" -ErrorAction SilentlyContinue |
	Sort-Object { [version]$_.Directory.Parent.Name } | Select-Object -Last 1
if (!$makeappx) { throw 'makeappx.exe not found - install the Windows SDK.' }

# stage
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item "$stage\Assets" -ItemType Directory | Out-Null
Copy-Item (Join-Path $bin 'EverythingShell.dll'), (Join-Path $bin 'EverythingShellStub.exe') $stage
Copy-Item (Join-Path $PSScriptRoot 'AppxManifest.xml') $stage

# logos, made from res\Everything.ico
Add-Type -AssemblyName System.Drawing
$ico = Join-Path $root 'EverythingShell\res\Everything.ico'
@{ 'StoreLogo.png' = 50; 'Square44x44Logo.png' = 44; 'Square150x150Logo.png' = 150 }.GetEnumerator() | ForEach-Object {
	$size = $_.Value
	$icon = New-Object System.Drawing.Icon($ico, $size, $size)
	$bmp  = New-Object System.Drawing.Bitmap($size, $size)
	$g    = [System.Drawing.Graphics]::FromImage($bmp)
	$g.InterpolationMode = 'HighQualityBicubic'
	$g.DrawImage($icon.ToBitmap(), 0, 0, $size, $size)
	$g.Dispose()
	$bmp.Save((Join-Path "$stage\Assets" $_.Key), [System.Drawing.Imaging.ImageFormat]::Png)
	$bmp.Dispose(); $icon.Dispose()
}

& $makeappx.FullName pack /d $stage /p $msix /o
if ($LASTEXITCODE) { throw 'makeappx failed' }

Write-Host "Created $msix (unsigned)"
