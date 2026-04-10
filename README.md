# CrossPoint Reader (Personal Fork)

Personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) with article sync support.

This fork adds the ability to sync articles from a backend service, so new reading material can be pushed to the device over WiFi without manually transferring files.

## What's different from upstream

- **Article sync**: Fetches articles from a configurable backend (`GET /sync`), downloads new EPUBs to `/articles/`, and shows article titles (instead of filenames) in the file browser.
- **Unified "Recently Read"**: The home screen shows both books and articles in a single list.

Everything else is upstream CrossPoint Reader. This fork regularly pulls in upstream changes.

## Article sync setup

1. Flash the firmware to your Xteink X4
2. In the device settings, set the **Articles Backend URL** (defaults to `https://crosspoint.val.run`)
3. Go to **File Transfer > Sync Articles**, connect to WiFi, and the device fetches and downloads new articles

The backend should respond to `GET /sync` with:

```json
{
  "articles": [
    { "filename": "article.epub", "title": "Article Title" }
  ]
}
```

And serve individual files at `GET /article/<filename>`.

## Building and flashing

Requires PlatformIO. Clone with submodules:

```sh
git clone --recursive https://github.com/tijs/crosspoint-reader
cd crosspoint-reader
```

Build and flash:

```sh
pio run -e gh_release -t upload
```

## Releases

This fork uses its own release tags (`v1.2.0-tijs.1`, `v1.2.0-tijs.2`, etc.) based on the upstream version number. Pushing a tag triggers a GitHub Actions build that produces the firmware binary.

## Upstream

Based on [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader). See the upstream repo for full documentation, the user guide, contributing guidelines, and hardware details.
