@echo off

:: universe_rp_play.bat -- Windows client launcher

:: Main settings
set jk_executable_64=taystjk.x86_64.exe
set jk_executable_32=taystjk.x86.exe

:: GalaxyRP fix: [TaystJK] this script lives inside the GalaxyRP folder, one level below the
:: engine executables -- switch to that parent folder FIRST, using %~dp0 (this batch file's own
:: location) rather than relying on whatever folder it happened to be double-clicked or launched
:: from. This makes both the executable check below and "fs_homepath ." further down resolve
:: against the engine's own folder, not wherever the script's working directory started out.
:: GLX_U: [Branding] the mod folder is the one this script sits in, whatever it is called
for %%I in ("%~dp0.") do set "jk_mod_folder=%%~nxI"
cd /d "%~dp0.."

:: Executable check -- by this machine's architecture first, so a build it cannot run is never
:: picked. PROCESSOR_ARCHITECTURE is what this command window runs as; PROCESSOR_ARCHITEW6432 is set
:: only when that is a 32-bit window on 64-bit Windows, and then holds the real one. 32-bit Windows
:: (x86) looks for the 32-bit build only -- a 64-bit one fails there with "This app can't run on your
:: PC". Everything else prefers the 64-bit build and falls back to the 32-bit one: 64-bit Windows
:: (AMD64), and Windows on ARM (ARM64), for which TaystJK has no build of its own -- Windows 11 on ARM
:: runs the 64-bit one, Windows 10 on ARM only the 32-bit one. Bail out with an error instead of
:: trying to launch something that isn't there.
set jk_arch=%PROCESSOR_ARCHITECTURE%
if defined PROCESSOR_ARCHITEW6432 set jk_arch=%PROCESSOR_ARCHITEW6432%
set jk_executable=
if /i "%jk_arch%"=="x86" (
	if exist "%jk_executable_32%" set jk_executable=%jk_executable_32%
) else if exist "%jk_executable_64%" (
	set jk_executable=%jk_executable_64%
) else if exist "%jk_executable_32%" (
	set jk_executable=%jk_executable_32%
)
if not defined jk_executable (
	if /i "%jk_arch%"=="x86" (
		echo ERROR: Could not find %jk_executable_32% next to the "%jk_mod_folder%" folder ^(this is 32-bit Windows^).
		if exist "%jk_executable_64%" echo %jk_executable_64% is there, but a 64-bit build cannot run on 32-bit Windows: install the x86 build.
	) else (
		echo ERROR: Could not find %jk_executable_64% or %jk_executable_32% next to the "%jk_mod_folder%" folder.
	)
	echo Make sure a TaystJK client build is installed alongside "%jk_mod_folder%".
	pause
	exit /b 1
)

:: Welcome message
echo   ____________________________________________________________
echo "| __________________________________________________________ |"
echo "||  _    _       _                           _____  _____   ||"
echo "|| | |  | |     (_)                         |  __ \|  __ \  ||"
echo "|| | |  | |_ __  ___   _____ _ __ ___  ___  | |__) | |__) | ||"
echo "|| | |  | | '_ \| \ \ / / _ \ '__/ __|/ _ \ |  _  /|  ___/  ||"
echo "|| | |__| | | | | |\ V /  __/ |  \__ \  __/ | | \ \| |      ||"
echo "||  \____/|_| |_|_| \_/ \___|_|  |___/\___| |_|  \_\_|      ||"
echo "||                                                          ||"
echo "||                  A JKA ROLEPLAYING MOD                   ||"
echo "||__________________________________________________________||"
echo "|____________________________________________________________|"
echo.

:: Show options
echo [1] Press ENTER to play locally
set /p option=[2] Type or paste an IP to connect:

:: Check options. fs_portable 1 + fs_homepath . keep every file the client writes (settings,
:: saved profiles, screenshots) inside this folder instead of the Windows user profile.
:: GalaxyRP fix: [TaystJK] dropped "+set net_port 29070 +set dedicated 0 +exec server.cfg" from
:: the "play locally" branch -- those are dedicated-server-only settings (server.cfg isn't even
:: this mod's config filename, that's universe_rp_server.cfg) that don't belong on a client launch
:: and appear to have been copy-pasted here by mistake.
:: GLX_U: [TaystJK] fs_forcegame "" -- no forced folder. The TaystJK client's default (taystjk) would
:: send every write to taystjk/. Empty, the mod folder (fs_game) is read and written alone, with its own
:: taystjk.cfg, and another server's mod gets its own folder without ours layered under it.
if "%option%" == "" (
	start "" %jk_executable% +set fs_portable 1 +set fs_homepath . +set fs_game "%jk_mod_folder%" +set fs_forcegame ""
) else (
	start "" %jk_executable% +set fs_portable 1 +set fs_homepath . +set fs_game "%jk_mod_folder%" +set fs_forcegame "" +connect %option%
)
