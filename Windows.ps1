# Windows.ps1 — Windows development dependencies for xbax-stream.
#
# This is the Windows analogue of the macOS Brewfile. It installs everything
# needed to:
#   * configure and build the CMake project,
#   * bootstrap the vendored Go 1.24.13 toolchain (needs an existing `go`),
#   * build / cross-compile the LLVM/Clang engine and `cleng.exe`
#     (Windows is the *native* target here — no MinGW cross step required),
#   * provide Linux/glibc cross sysroots for x86_64-linux-gnu and
#     aarch64-linux-gnu via LLVM + lld (Windows hosts can't use Apple's
#     `messense/macos-cross-toolchains` taps),
#   * run the Python streaming client (`main.py`).
#
# Run from an elevated PowerShell:
#
#     Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
#     .\Windows.ps1
#
# Most packages come from winget (built in to Windows 10/11). A few
# Python packages come from pip. The script is idempotent: re-running it
# upgrades anything already installed.

[CmdletBinding()]
param(
    [switch]$SkipPython,
    [switch]$SkipLinuxSysroots
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Set-WrapOutput {
    # Make wide output (winget tables, MSBuild error paths, wsl warnings) wrap
    # to the window width instead of being truncated with "...". Affects both
    # PowerShell's formatter ($PSDefaultParameterValues for Out-* / Format-*)
    # and the underlying console buffer width that native commands honour.
    $wide = 32766  # PowerShell's max for Out-String / Format-Table
    $PSDefaultParameterValues['Out-File:Width']     = $wide
    $PSDefaultParameterValues['Out-String:Width']   = $wide
    $PSDefaultParameterValues['Format-Table:Wrap'] = $true
    $PSDefaultParameterValues['Format-List:Wrap']  = $true

    try {
        $raw = $Host.UI.RawUI
        $win = $raw.WindowSize
        # Set the screen buffer width equal to the window width so native
        # console programs (winget, wsl, msbuild) line-wrap instead of being
        # clipped by a wider buffer.
        $buf = $raw.BufferSize
        if ($buf.Width -ne $win.Width) {
            $raw.BufferSize = [System.Management.Automation.Host.Size]::new($win.Width, [Math]::Max($buf.Height, 9999))
        }
    } catch {
        # Some hosts (Windows Terminal, VS Code integrated terminal) don't
        # allow resizing the buffer — they already wrap by default, so this
        # is harmless to skip.
    }
}

Set-WrapOutput

function Test-Admin {
    $current = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($current)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-SelfElevate {
    # Re-launch this script elevated so each winget call doesn't trigger its
    # own UAC prompt (which is the usual cause of "You cancelled the install"
    # when the dialog is dismissed or hidden behind another window).
    if (Test-Admin) { return }
    Write-Host "==> Re-launching elevated (accept the UAC prompt)..." -ForegroundColor Cyan

    # Use the same PowerShell host that's running us (pwsh.exe vs powershell.exe).
    $hostExe = (Get-Process -Id $PID).Path
    if (-not $hostExe) {
        $hostExe = if ($PSVersionTable.PSEdition -eq 'Core') { 'pwsh.exe' } else { 'powershell.exe' }
    }

    # Prefer Windows Terminal for the elevated child so output wraps to the
    # window width instead of being clipped by conhost's fixed 120-col buffer.
    $wt = Get-Command wt.exe -ErrorAction SilentlyContinue

    # Keep the elevated console open long enough to read errors. Pause at the
    # end so the window doesn't auto-close on success/failure.
    $inner = "& `"$PSCommandPath`""
    if ($SkipPython)        { $inner += ' -SkipPython' }
    if ($SkipLinuxSysroots) { $inner += ' -SkipLinuxSysroots' }
    $inner += "; Write-Host ''; Read-Host 'Press Enter to close'"

    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $inner)

    try {
        if ($wt) {
            # `wt -- <host> <args>` opens a Windows Terminal tab that wraps
            # by default; we still need RunAs on wt itself for elevation.
            $wtArgs = @('--', $hostExe) + $argList
            $proc = Start-Process -FilePath $wt.Source -ArgumentList $wtArgs `
                -Verb RunAs -Wait -PassThru -ErrorAction Stop
        } else {
            $proc = Start-Process -FilePath $hostExe -ArgumentList $argList `
                -Verb RunAs -Wait -PassThru -ErrorAction Stop
        }
        exit $proc.ExitCode
    } catch [System.ComponentModel.Win32Exception] {
        # 1223 == ERROR_CANCELLED (user clicked No on the UAC prompt).
        if ($_.Exception.NativeErrorCode -eq 1223) {
            throw "Elevation was cancelled at the UAC prompt. Re-run and click Yes."
        }
        throw
    }
}

function Require-Command {
    param([Parameter(Mandatory)] [string]$Name, [string]$Hint)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' not found on PATH.$( if ($Hint) { " $Hint" } )"
    }
}

function Install-Winget {
    param(
        [Parameter(Mandatory)] [string]$Id,
        [string]$Comment,
        [string]$Override
    )
    if ($Comment) { Write-Host "==> $Comment" -ForegroundColor Cyan }
    Write-Host "    winget install --id $Id" -ForegroundColor DarkGray
    $args = @(
        'install', '--id', $Id,
        '--exact',
        '--accept-package-agreements',
        '--accept-source-agreements',
        '--silent',
        '--disable-interactivity'
    )
    if ($Override) { $args += @('--override', $Override) }
    & winget @args 2>&1 | Out-Host
    $code = $LASTEXITCODE
    # winget exit codes: 0 = installed, 0x8A15002B (-1978335189) = already installed,
    # 0x8A150011 (-1978335215) = no upgrade available.
    $okCodes = @(0, -1978335189, -1978335215)
    if ($okCodes -notcontains $code) {
        Write-Warning "winget exited with code $code for $Id (continuing)."
    }
}

Invoke-SelfElevate
Require-Command winget "Install 'App Installer' from the Microsoft Store on Windows 10."

function Set-GitBashFirstOnPath {
    # CMake custom commands in CMakeLists.txt invoke `bash <script>.sh`. On
    # Windows that needs to resolve to Git Bash (which understands C:\... paths)
    # and NOT to C:\Windows\System32\bash.exe (the WSL launcher, which treats
    # Windows paths as literal Linux paths and prints "No such file or directory").
    $gitBashDirs = @(
        'C:\Program Files\Git\bin',
        'C:\Program Files\Git\usr\bin',
        'C:\Program Files (x86)\Git\bin'
    ) | Where-Object { Test-Path (Join-Path $_ 'bash.exe') }
    if (-not $gitBashDirs) { return }

    foreach ($scope in 'Process','User') {
        $cur = [Environment]::GetEnvironmentVariable('Path', $scope)
        if (-not $cur) { $cur = '' }
        $parts = $cur -split ';' | Where-Object { $_ -and ($gitBashDirs -notcontains $_) }
        $new = (@($gitBashDirs) + $parts) -join ';'
        if ($new -ne $cur) {
            [Environment]::SetEnvironmentVariable('Path', $new, $scope)
        }
    }
    Write-Host "==> Prepended Git Bash to PATH so CMake's `bash <script>.sh` calls work" -ForegroundColor Cyan
}

# --- Build system ------------------------------------------------------------
Install-Winget -Id 'Kitware.CMake'           -Comment 'CMake'
Install-Winget -Id 'Ninja-build.Ninja'       -Comment 'Ninja generator'
# Git's installer is Inno Setup; winget's --silent alone is not enough to
# fully suppress its UI on some versions, so pass the real Inno flags too.
Install-Winget -Id 'Git.Git'                 -Comment 'Git' `
    -Override '/VERYSILENT /NORESTART /SUPPRESSMSGBOXES /NOCANCEL /NOICONS'
Set-GitBashFirstOnPath
# pkg-config equivalent on Windows; pulled in via the MSYS2 base install below
# (provides `pkgconf`, used by some llvm-project subprojects).

# --- Host toolchains used as bootstraps -------------------------------------
# LLVM ships clang / clang++ / lld-link that the LLVM build uses for the
# native tablegen tools (llvm-tblgen, clang-tblgen) before the main stage,
# matching the macOS `brew "llvm"` role. On Windows it is *also* the native
# target compiler, so no separate MinGW cross stage is needed for cleng.exe.
Install-Winget -Id 'LLVM.LLVM'               -Comment 'LLVM + clang + lld (host & target)'
Install-Winget -Id 'GoLang.Go'               -Comment 'Go (bootstrap for the vendored go/ toolchain)'

# Visual Studio Build Tools provide the MSVC linker, Windows SDK, and
# import libraries that clang-cl / lld-link need when targeting native
# Windows. Without these, building cleng.exe and the LLVM Windows static
# libs will fail to find kernel32.lib / ucrt etc.
Install-Winget -Id 'Microsoft.VisualStudio.2022.BuildTools' `
    -Comment 'MSVC Build Tools + Windows SDK (linker, libs, headers)'

# Python client + general scripting host. Use the same minor as the Brewfile.
if (-not $SkipPython) {
    Install-Winget -Id 'Python.Python.3.12'  -Comment 'Python 3.12 (xbax-stream client)'
}

# MSYS2 gives us a Unix-y shell + pkgconf + make for the handful of build
# scripts under campiler/ and cleng/scripts/ that expect a POSIX environment.
Install-Winget -Id 'MSYS2.MSYS2'             -Comment 'MSYS2 (POSIX shell, pkgconf, make)'

# --- Linux / GNU cross sysroots ---------------------------------------------
# The macOS Brewfile installs Messense's prebuilt GCC cross toolchains purely
# to harvest glibc headers + crt objects + libstdc++/libgcc into the per-
# triplet bundles under cleng/sysroot/. Those packages don't exist for
# Windows hosts. Two practical options:
#
#   1. Use WSL2 + the matching `gcc-x86-64-linux-gnu` /
#      `gcc-aarch64-linux-gnu` packages, then run the existing copy_sysroot
#      script from inside WSL.
#   2. Download a prebuilt sysroot tarball (e.g. from the LLVM project or
#      from a CI artifact) and unpack it into cleng/sysroot/.
#
# We prefer option (1) here because it tracks upstream glibc versions
# without us hosting binaries.
if (-not $SkipLinuxSysroots) {
    Install-Winget -Id 'Microsoft.WSL'       -Comment 'WSL2 (host for Linux cross sysroot harvesting)'
    # Refresh the WSL2 kernel package — without this, `wsl <anything>` prints
    # "WSL 2 requires an update to its kernel component" and exits non-zero,
    # which breaks downstream MSBuild custom-build steps.
    Write-Host "==> wsl --update" -ForegroundColor Cyan
    & wsl.exe --update 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "wsl --update exited with code $LASTEXITCODE (continuing)."
    }
    Write-Host ""
    Write-Host "Next step for Linux sysroots (run once, in an elevated prompt):" -ForegroundColor Yellow
    Write-Host "    wsl --install -d Ubuntu" -ForegroundColor Yellow
    Write-Host "    wsl -d Ubuntu -- sudo apt-get update" -ForegroundColor Yellow
    Write-Host "    wsl -d Ubuntu -- sudo apt-get install -y \\" -ForegroundColor Yellow
    Write-Host "        gcc-x86-64-linux-gnu g++-x86-64-linux-gnu \\" -ForegroundColor Yellow
    Write-Host "        gcc-aarch64-linux-gnu g++-aarch64-linux-gnu" -ForegroundColor Yellow
    Write-Host "Then run cleng/scripts/copy_sysroot.cmake from inside WSL." -ForegroundColor Yellow
}

# --- Python client packages -------------------------------------------------
if (-not $SkipPython) {
    $req = Join-Path $PSScriptRoot 'requirements.txt'
    if (Test-Path $req) {
        Write-Host "==> pip install -r requirements.txt" -ForegroundColor Cyan
        # Re-resolve python after the winget install above so PATH is fresh.
        $py = (Get-Command py -ErrorAction SilentlyContinue) `
            ?? (Get-Command python -ErrorAction SilentlyContinue)
        if ($null -eq $py) {
            Write-Warning "python not found on PATH yet — open a new shell and run: py -m pip install -r requirements.txt"
        } else {
            & $py.Source -m pip install --upgrade pip
            & $py.Source -m pip install -r $req
        }
    } else {
        Write-Warning "requirements.txt not found next to Windows.ps1 — skipping pip install."
    }
}

Write-Host ""
Write-Host "All Windows dependencies requested. Open a new shell so PATH updates take effect." -ForegroundColor Green
