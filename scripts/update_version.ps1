$version = git describe --tags --always --dirty
if ($LASTEXITCODE -ne 0) {
    Write-Host "Git command failed. Using default version."
    $version = "v0.0.0-unknown"
}

# Parse version string (e.g., v0.1.3-2-gd50f900)
if ($version -match "v?(\d+)\.(\d+)\.(\d+)") {
    $major = $matches[1]
    $minor = $matches[2]
    $patch = $matches[3]
} else {
    $major = 0
    $minor = 0
    $patch = 0
}

# Get Git Hash
$gitHash = git rev-parse --short=8 HEAD
if ($LASTEXITCODE -ne 0) { $gitHash = "00000000" }

# Get Timestamp
$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm"

$headerContent = @"
#ifndef APP_VERSION_H
#define APP_VERSION_H

#define FIRMWARE_VERSION "$version"
#define FW_VER_MAJOR $major
#define FW_VER_MINOR $minor
#define FW_VER_PATCH $patch
#define FW_GIT_HASH "$gitHash"
#define FW_BUILD_TIME "$timestamp"

#endif // APP_VERSION_H
"@

$headerPath = Join-Path $PSScriptRoot "..\Core\Inc\app_version.h"
Set-Content -Path $headerPath -Value $headerContent
Write-Host "Updated app_version.h with version: $version"
