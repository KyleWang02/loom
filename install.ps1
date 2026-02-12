# Loom installer for Windows
# Usage: irm https://raw.githubusercontent.com/KyleWang02/loom/main/install.ps1 | iex

param(
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"
$Repo = "KyleWang02/loom"
$Arch = "x86_64"

# Determine version
if ($Version -eq "") {
    if ($env:LOOM_VERSION) {
        $Version = $env:LOOM_VERSION
    } else {
        Write-Host "Fetching latest release..."
        try {
            $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest"
            $Version = $release.tag_name
        } catch {
            Write-Error "Could not determine latest version: $_"
            exit 1
        }
    }
}

Write-Host "Installing loom $Version (windows-$Arch)..."

# Build download URL
$archive = "loom-$Version-windows-$Arch.zip"
$url = "https://github.com/$Repo/releases/download/$Version/$archive"

# Create temp directory
$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) "loom-install-$(Get-Random)"
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null

try {
    # Download
    $zipPath = Join-Path $tmpDir $archive
    Write-Host "Downloading $url..."
    try {
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
    } catch {
        Write-Error "Download failed. Check that release $Version exists for windows-$Arch."
        exit 1
    }

    # Extract
    Expand-Archive -Path $zipPath -DestinationPath $tmpDir -Force

    # Find the binary
    $binary = Get-ChildItem -Path $tmpDir -Filter "loom.exe" -Recurse | Select-Object -First 1
    if (-not $binary) {
        Write-Error "Binary not found in archive"
        exit 1
    }

    # Install to %LOCALAPPDATA%\loom\bin
    $installDir = Join-Path $env:LOCALAPPDATA "loom\bin"
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    Copy-Item -Path $binary.FullName -Destination (Join-Path $installDir "loom.exe") -Force

    Write-Host "Installed loom to $installDir\loom.exe"

    # Add to user PATH if not already present
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if ($userPath -notlike "*$installDir*") {
        [Environment]::SetEnvironmentVariable("Path", "$installDir;$userPath", "User")
        Write-Host "Added $installDir to user PATH."
        Write-Host ""
        Write-Host "Please restart your terminal for PATH changes to take effect."
    }

    Write-Host ""
    Write-Host "Run 'loom --version' to verify the installation."

} finally {
    # Cleanup
    Remove-Item -Path $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
}
