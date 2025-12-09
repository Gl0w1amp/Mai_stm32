$version = git describe --tags --always --dirty
if ($LASTEXITCODE -ne 0) {
    Write-Host "Git command failed. Using default version."
    $version = "v0.0.0-unknown"
}

$headerContent = @"
#ifndef APP_VERSION_H
#define APP_VERSION_H

#define FIRMWARE_VERSION "$version"

#endif // APP_VERSION_H
"@

$headerPath = Join-Path $PSScriptRoot "..\Core\Inc\app_version.h"
Set-Content -Path $headerPath -Value $headerContent
Write-Host "Updated app_version.h with version: $version"
