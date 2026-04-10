#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

/**
 * Activity for syncing articles from the CrossPoint Articles backend.
 *
 * Flow:
 * 1. Check/connect WiFi
 * 2. Fetch article list from backend (GET /sync)
 * 3. Diff against local /articles/ directory
 * 4. Download missing EPUBs with progress
 * 5. Show result and return
 */
class ArticleSyncActivity final : public Activity {
 public:
  explicit ArticleSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ArticleSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  enum State {
    FETCHING_LIST,
    DOWNLOADING,
    COMPLETE,
    ERROR,
    NO_URL,
  };

  struct ArticleInfo {
    std::string filename;
    std::string title;
  };

  State state = FETCHING_LIST;
  std::string statusMessage;

  // Articles to download
  std::vector<ArticleInfo> toDownload;
  int downloadIndex = 0;
  int downloadedCount = 0;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;

  void onWifiSelectionComplete(bool success);
  void fetchArticleList();
  void downloadArticles();
  bool downloadSingleArticle(const ArticleInfo& article);

  static bool isValidArticleFilename(const char* name);
};
