#!/bin/bash
# Uploads Threadlock to Google Play through the Play Developer API (Git Bash: openssl + curl).
#   [RELEASE_STATUS=draft] play_upload.sh <service-account.json> <app.aab> [track] [--commit]
# One "edit" holds everything: the bundle on the track with the release notes from listing.md, the
# listing text (name, short and full description),
# the store graphics (phone screenshots in listing.md's order, feature graphic, icon).
# Without --commit the edit is only validated and then thrown away (a dry run).
set -euo pipefail
export MSYS_NO_PATHCONV=1

KEY="$1"; AAB="$2"; TRACK="${3:-alpha}"; COMMIT="${4:-}"
STATUS="${RELEASE_STATUS:-completed}"  # RELEASE_STATUS=draft: a draft app (never published) only takes draft releases
PKG=com.shmoffeestudios.threadlock
LANG_CODE=en-US  # the listing's default language on Play
STORE="$(cygpath -m "$(cd "$(dirname "$0")" && pwd)")"
API=https://androidpublisher.googleapis.com/androidpublisher/v3/applications/$PKG
UPLOAD=https://androidpublisher.googleapis.com/upload/androidpublisher/v3/applications/$PKG

# ---- Access token: a JWT signed with the service account's key ----
b64() { openssl base64 -e -A | tr '+/' '-_' | tr -d '='; }
field() { grep -o "\"$1\": *\"[^\"]*\"" "$KEY" | sed 's/.*: *"//;s/"$//'; }
PEM="$(cygpath -m "$(mktemp)")"; trap 'rm -f "$PEM"' EXIT
printf '%b' "$(field private_key)" > "$PEM"
NOW=$(date +%s)
HEADER=$(printf '{"alg":"RS256","typ":"JWT"}' | b64)
CLAIMS=$(printf '{"iss":"%s","scope":"https://www.googleapis.com/auth/androidpublisher","aud":"https://oauth2.googleapis.com/token","iat":%d,"exp":%d}' \
  "$(field client_email)" "$NOW" $((NOW + 3000)) | b64)
SIG=$(printf '%s.%s' "$HEADER" "$CLAIMS" | openssl dgst -sha256 -sign "$PEM" | b64)
rm -f "$PEM"
TOKEN=$(curl -s -X POST https://oauth2.googleapis.com/token \
  -d "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=$HEADER.$CLAIMS.$SIG" |
  grep -o '"access_token": *"[^"]*"' | sed 's/.*: *"//;s/"$//')
[ -n "$TOKEN" ] || { echo "no access token"; exit 1; }
AUTH="Authorization: Bearer $TOKEN"

# call METHOD URL [curl args...]: prints the body, fails on HTTP >= 400
call() {
  local method=$1 url=$2; shift 2
  local out; out=$(curl -s -w '\n%{http_code}' -X "$method" -H "$AUTH" "$@" "$url")
  local code=${out##*$'\n'}; local body=${out%$'\n'*}
  if [ "$code" -ge 400 ]; then echo "HTTP $code from $method $url" >&2; echo "$body" >&2; return 1; fi
  echo "$body"
}
json_string() { sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' | awk 'BEGIN{ORS=""} NR>1{print "\\n"} {print}'; }

EDIT=$(call POST "$API/edits" -H "Content-Length: 0" | grep -o '"id": *"[^"]*"' | sed 's/.*: *"//;s/"$//')
echo "edit $EDIT"
abandon() { [ -n "${EDIT:-}" ] && curl -s -X DELETE -H "$AUTH" "$API/edits/$EDIT" >/dev/null; }
trap 'rm -f "$PEM"; [ "${DONE:-}" = 1 ] || { echo "abandoning the edit"; abandon; }' EXIT

# ---- The bundle and the release on the track ----
if [[ "$AAB" =~ ^[0-9]+$ ]]; then
  VC=$AAB; echo "bundle already on Play: versionCode $VC"  # a number instead of a file: re-send notes and graphics
else
  VC=$(call POST "$UPLOAD/edits/$EDIT/bundles?uploadType=media" -H "Content-Type: application/octet-stream" \
    --data-binary "@$AAB" | grep -o '"versionCode": *[0-9]*' | grep -o '[0-9]*$')
  echo "bundle uploaded: versionCode $VC"
fi
VERSION_NAME=$(grep -o 'versionName = "[^"]*"' "$STORE/../app/build.gradle.kts" | sed 's/.*"\(.*\)"/\1/')
NOTES=$(sed -n "/^## Release notes for $VERSION_NAME /,/^## /p" "$STORE/listing.md" | sed '1d;/^## /d' |
  sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' | json_string)
[ -n "$NOTES" ] || { echo "no release notes for $VERSION_NAME in listing.md"; exit 1; }
# The body goes through a file: Windows curl mangles non-ASCII text (the bullets) passed as an argument.
BODY="$(cygpath -m "$(mktemp)")"
printf '%s' "{\"track\":\"$TRACK\",\"releases\":[{\"name\":\"$VERSION_NAME ($VC)\",\"versionCodes\":[\"$VC\"],\"status\":\"$STATUS\",\"releaseNotes\":[{\"language\":\"$LANG_CODE\",\"text\":\"$NOTES\"}]}]}" > "$BODY"
call PUT "$API/edits/$EDIT/tracks/$TRACK" -H "Content-Type: application/json; charset=utf-8" --data-binary "@$BODY" >/dev/null
rm -f "$BODY"
echo "track $TRACK: $VERSION_NAME ($VC)"

# ---- Store listing text: the app name and both descriptions from listing.md ----
section() { sed -n "/^## $1/,/^## /p" "$STORE/listing.md" | sed '1d;/^## /d' | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}'; }
TITLE=$(section "App name" | json_string)
SHORT=$(section "Short description" | json_string)
FULL=$(section "Full description" | json_string)
[ -n "$TITLE" ] && [ -n "$SHORT" ] && [ -n "$FULL" ] || { echo "listing.md lacks the name or a description"; exit 1; }
BODY="$(cygpath -m "$(mktemp)")"
printf '%s' "{\"title\":\"$TITLE\",\"shortDescription\":\"$SHORT\",\"fullDescription\":\"$FULL\"}" > "$BODY"
call PATCH "$API/edits/$EDIT/listings/$LANG_CODE" -H "Content-Type: application/json; charset=utf-8" --data-binary "@$BODY" >/dev/null
rm -f "$BODY"
echo "listing $LANG_CODE: name, short and full description"

# ---- Store graphics: replace each set ----
images() {  # images TYPE file...
  local type=$1; shift
  call DELETE "$API/edits/$EDIT/listings/$LANG_CODE/$type" >/dev/null
  for f in "$@"; do
    call POST "$UPLOAD/edits/$EDIT/listings/$LANG_CODE/$type?uploadType=media" -H "Content-Type: image/png" \
      --data-binary "@$f" >/dev/null
    echo "  $type <- $(basename "$f")"
  done
}
ORDER=$(sed -n '/upload in this order:/,/^$/p' "$STORE/listing.md" | tr ',\n' '  ' | sed 's/.*order://; s/(.*//')
SHOTS=(); for n in $ORDER; do SHOTS+=("$STORE/screenshots/$n.png"); done
images phoneScreenshots "${SHOTS[@]}"
images featureGraphic "$STORE/feature-graphic.png"
images icon "$STORE/icon-512.png"

# ---- Validate, then commit or throw away ----
call POST "$API/edits/$EDIT:validate" -H "Content-Length: 0" >/dev/null
echo "edit is valid"
if [ "$COMMIT" = "--commit" ]; then
  call POST "$API/edits/$EDIT:commit" -H "Content-Length: 0" >/dev/null
  DONE=1; echo "COMMITTED: live on the $TRACK track once Google's review passes"
else
  echo "dry run: not committed"
fi
