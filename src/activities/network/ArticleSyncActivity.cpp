#include "ArticleSyncActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstring>

#include "ArticlesPaths.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {
void wifiOff() {
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}
}  // namespace

bool ArticleSyncActivity::isValidArticleFilename(const char* name) {
  if (!name || name[0] == '\0' || name[0] == '.') return false;
  if (strlen(name) > 120) return false;
  for (const char* p = name; *p; ++p) {
    if (*p == '/' || *p == '\\' || *p < 0x20) return false;
  }
  if (strstr(name, "..") != nullptr) return false;
  return true;
}

void ArticleSyncActivity::onEnter() {
  Activity::onEnter();

  // Check if backend URL is configured
  if (strlen(SETTINGS.articlesBackendUrl) == 0) {
    state = NO_URL;
    requestUpdate();
    return;
  }

  // Check if already connected
  if (WiFi.status() == WL_CONNECTED) {
    LOG_DBG("ArtSync", "Already connected to WiFi");
    onWifiSelectionComplete(true);
    return;
  }

  // Launch WiFi selection subactivity
  LOG_DBG("ArtSync", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void ArticleSyncActivity::onExit() {
  Activity::onExit();
  wifiOff();
}

void ArticleSyncActivity::onWifiSelectionComplete(bool success) {
  if (!success) {
    LOG_DBG("ArtSync", "WiFi connection failed, exiting");
    finish();
    return;
  }

  LOG_DBG("ArtSync", "WiFi connected, fetching article list");

  {
    RenderLock lock(*this);
    state = FETCHING_LIST;
    statusMessage = tr(STR_FETCHING_ARTICLE_LIST);
  }
  requestUpdate(true);

  fetchArticleList();
}

void ArticleSyncActivity::fetchArticleList() {
  std::string url = std::string(SETTINGS.articlesBackendUrl) + "/sync";

  // Build metadata doc and download list from JSON response.
  // The raw JSON string is scoped so it's freed before metadata serialization.
  JsonDocument metaDoc;
  toDownload.clear();

  {
    std::string json;
    if (!HttpDownloader::fetchUrl(url, json)) {
      LOG_ERR("ArtSync", "Failed to fetch article list from %s", url.c_str());
      {
        RenderLock lock(*this);
        state = ERROR;
        statusMessage = tr(STR_FETCH_FEED_FAILED);
      }
      requestUpdate(true);
      return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
      LOG_ERR("ArtSync", "JSON parse error: %s", err.c_str());
      {
        RenderLock lock(*this);
        state = ERROR;
        statusMessage = tr(STR_PARSE_FEED_FAILED);
      }
      requestUpdate(true);
      return;
    }

    // json string is still alive here but doc borrows from it, so keep both in scope
    JsonArray articles = doc["articles"].as<JsonArray>();
    toDownload.reserve(articles.size());

    for (JsonObject article : articles) {
      const char* filename = article["filename"];
      const char* title = article["title"];
      if (!filename) continue;

      if (!isValidArticleFilename(filename)) {
        LOG_ERR("ArtSync", "Invalid filename rejected: %s", filename);
        continue;
      }

      const char* safeTitle = title ? title : tr(STR_UNNAMED);
      metaDoc[filename] = safeTitle;

      char localPath[160];
      snprintf(localPath, sizeof(localPath), "%s%s", ARTICLES_PATH, filename);
      if (!Storage.exists(localPath)) {
        toDownload.push_back({filename, safeTitle});
      }
    }
  }  // json + doc freed here, before metadata serialization

  // Serialize metaDoc directly to file — no intermediate String buffer
  {
    HalFile file;
    if (Storage.openFileForWrite("ArtSync", "/articles/.metadata.json", file)) {
      serializeJson(metaDoc, file);
      file.close();
    } else {
      LOG_ERR("ArtSync", "Failed to write metadata file");
    }
  }

  if (toDownload.empty()) {
    LOG_DBG("ArtSync", "No new articles to download");
    {
      RenderLock lock(*this);
      state = COMPLETE;
      statusMessage = tr(STR_SYNC_NO_NEW);
    }
    requestUpdate(true);
    return;
  }

  LOG_DBG("ArtSync", "%d new articles to download", static_cast<int>(toDownload.size()));
  downloadArticles();
}

void ArticleSyncActivity::downloadArticles() {
  downloadIndex = 0;
  downloadedCount = 0;

  {
    RenderLock lock(*this);
    state = DOWNLOADING;
  }
  requestUpdate(true);

  for (downloadIndex = 0; downloadIndex < static_cast<int>(toDownload.size()); downloadIndex++) {
    {
      RenderLock lock(*this);
      downloadProgress = 0;
      downloadTotal = 0;
    }
    requestUpdateAndWait();

    if (downloadSingleArticle(toDownload[downloadIndex])) {
      downloadedCount++;
    } else {
      LOG_ERR("ArtSync", "Failed to download: %s", toDownload[downloadIndex].filename.c_str());
    }
  }

  {
    RenderLock lock(*this);
    state = COMPLETE;
    char buf[64];
    snprintf(buf, sizeof(buf), tr(STR_SYNC_COMPLETE), downloadedCount);
    statusMessage = buf;
  }
  requestUpdate(true);
}

bool ArticleSyncActivity::downloadSingleArticle(const ArticleInfo& article) {
  char url[256];
  char tempPath[160];
  char finalPath[160];
  snprintf(url, sizeof(url), "%s/article/%s", SETTINGS.articlesBackendUrl, article.filename.c_str());
  snprintf(tempPath, sizeof(tempPath), "/articles/.dl_%s", article.filename.c_str());
  snprintf(finalPath, sizeof(finalPath), "/articles/%s", article.filename.c_str());

  LOG_DBG("ArtSync", "Downloading %s", article.filename.c_str());

  auto result = HttpDownloader::downloadToFile(url, tempPath, [this](size_t downloaded, size_t total) {
    {
      RenderLock lock(*this);
      downloadProgress = downloaded;
      downloadTotal = total;
    }
    requestUpdate(true);
  });

  if (result != HttpDownloader::OK) {
    LOG_ERR("ArtSync", "Download failed for %s: %d", article.filename.c_str(), result);
    Storage.remove(tempPath);
    return false;
  }

  // Atomic rename: temp -> final
  if (!Storage.rename(tempPath, finalPath)) {
    LOG_ERR("ArtSync", "Rename failed: %s -> %s", tempPath, finalPath);
    Storage.remove(tempPath);
    return false;
  }

  LOG_DBG("ArtSync", "Downloaded: %s", article.filename.c_str());
  return true;
}

void ArticleSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int centerY = (pageHeight - lineHeight) / 2;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SYNC_ARTICLES));

  if (state == FETCHING_LIST) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, statusMessage.c_str(), true, EpdFontFamily::BOLD);
  } else if (state == DOWNLOADING) {
    const int startY = centerY - lineHeight;
    char progressStr[64];
    snprintf(progressStr, sizeof(progressStr), tr(STR_DOWNLOADING_ARTICLE), downloadIndex + 1,
             static_cast<int>(toDownload.size()));
    renderer.drawCenteredText(UI_10_FONT_ID, startY, progressStr, true, EpdFontFamily::BOLD);

    if (downloadIndex < static_cast<int>(toDownload.size())) {
      renderer.drawCenteredText(UI_10_FONT_ID, startY + lineHeight + metrics.verticalSpacing,
                                toDownload[downloadIndex].title.c_str());
    }

    if (downloadTotal > 0) {
      char bytesStr[32];
      snprintf(bytesStr, sizeof(bytesStr), "%zuKB / %zuKB", downloadProgress / 1024, downloadTotal / 1024);
      renderer.drawCenteredText(UI_10_FONT_ID, startY + 2 * (lineHeight + metrics.verticalSpacing), bytesStr);
    }
  } else {
    // Terminal states: NO_URL, COMPLETE, ERROR
    if (state == NO_URL) {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_NO_BACKEND_URL), true, EpdFontFamily::BOLD);
    } else if (state == COMPLETE) {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY, statusMessage.c_str(), true, EpdFontFamily::BOLD);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight / 2, tr(STR_SYNC_FAILED), true,
                                EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, centerY + lineHeight / 2 + metrics.verticalSpacing,
                                statusMessage.c_str());
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

void ArticleSyncActivity::loop() {
  if (state == COMPLETE || state == ERROR || state == NO_URL) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
    }
  }
}

bool ArticleSyncActivity::preventAutoSleep() { return state == FETCHING_LIST || state == DOWNLOADING; }
