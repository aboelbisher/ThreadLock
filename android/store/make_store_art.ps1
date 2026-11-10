# Builds Threadlock's Google Play graphics with headless Edge from the pages in art/: the 512 px icon
# (art/icon.svg), the 1024 x 500 feature graphic (art/feature.html) and captioned 1080 x 1920 screenshots
# (art/shot.html around raw phone captures).
# Raw captures: the release build on the Pixel 4 (1080 x 2280), taken with the dev cvars, e.g.
#   adb shell "am start -n com.shmoffeestudios.threadlock/com.aboelbisher.threadlock.ThreadlockActivity --es wlw 'tl.open=3-7;tl.solve=1;tl.autoplay=1'"
#   cmd /c "adb exec-out screencap -p > <Shots>\canal.png"
# finale = 1-15 just after "Every lantern lit" (~38 s), canal = 3-7 (5.5 s), apothecary = 2-12 (7 s),
# sanctum = 4-9 (6 s), lantern = 1-9 (5 s).
param(
  [string]$Shots = "C:\Users\User\AppData\Local\Temp\threadlock_shots",
  [string]$Out = $PSScriptRoot,
  [switch]$NoScreenshots  # only the icon and the feature graphic
)
$ErrorActionPreference = "Stop"
$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
$art = Join-Path $PSScriptRoot "art"
$profile = Join-Path $env:TEMP "threadlock_store_edge"

function Render($url, $w, $h, $png) {
  # A throwaway profile, so a running Edge is left alone; the page needs a moment for its font.
  # Edge reports "bytes written" on stderr, which Windows PowerShell would turn into a terminating error.
  $ErrorActionPreference = "Continue"
  & $edge --headless=new --disable-gpu --hide-scrollbars --allow-file-access-from-files `
    --user-data-dir="$profile" --virtual-time-budget=3000 --window-size="$w,$h" --screenshot="$png" $url 2>$null | Out-Null
  if (-not (Test-Path $png)) { throw "Edge did not write $png" }
  Write-Host "  $png"
}
function FileUrl($path) { return "file:///" + ((Resolve-Path $path).Path -replace '\\', '/') }

New-Item -ItemType Directory -Force (Join-Path $Out "screenshots") | Out-Null
Render (FileUrl "$art\icon.svg") 512 512 (Join-Path $Out "icon-512.png")
Render (FileUrl "$art\feature.html") 1024 500 (Join-Path $Out "feature-graphic.png")
if ($NoScreenshots) { return }

# name, raw capture, caption, subtitle, tint (each world's own light)
$cards = @(
  @("1_finale",     "finale",     "Light the window",       "Every machine is a puzzle in time", "#5a4720"),
  @("2_canal",      "canal",      "Rush hour",              "Canals one barge wide, both ways",  "#1d3550"),
  @("3_apothecary", "apothecary", "Fill every order",       "Measured out in doses",            "#4a3018"),
  @("4_sanctum",    "sanctum",    "Brew in layers",         "Recipes poured in the right order", "#3a2650"),
  @("5_lantern",    "lantern",    "Never let colours meet", "Place the pieces, tap play",       "#1f3a5a")
)
foreach ($c in $cards) {
  $raw = Join-Path $Shots "$($c[1]).png"
  if (-not (Test-Path $raw)) { Write-Warning "missing $raw"; continue }
  $q = "img=" + [uri]::EscapeDataString((FileUrl $raw)) + "&title=" + [uri]::EscapeDataString($c[2]) +
       "&sub=" + [uri]::EscapeDataString($c[3]) + "&tint=" + [uri]::EscapeDataString($c[4])
  Render ((FileUrl "$art\shot.html") + "?" + $q) 1080 1920 (Join-Path $Out "screenshots\$($c[0]).png")
}
