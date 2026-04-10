#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "ArticlesPaths.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Recents, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (!recentArticles.empty()) {
    count += recentArticles.size();
  }
  if (hasOpdsUrl) {
    count++;
  }
  return count;
}

void HomeActivity::loadRecents(std::vector<RecentBook>& out, int maxCount, bool articles) {
  out.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  out.reserve(std::min(static_cast<int>(books.size()), maxCount));

  for (const RecentBook& book : books) {
    if (static_cast<int>(out.size()) >= maxCount) break;
    const bool isArticle = book.path.compare(0, ARTICLES_PATH_LEN, ARTICLES_PATH) == 0;
    if (isArticle != articles) continue;
    if (!Storage.exists(book.path.c_str())) continue;
    out.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;

  selectorIndex = 0;

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecents(recentBooks, metrics.homeRecentBooksCount, false);
  loadRecents(recentArticles, metrics.homeRecentArticlesCount, true);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = renderer.getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Recent books come first
    if (selectorIndex < static_cast<int>(recentBooks.size())) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }

    // Then recent articles
    int idx = selectorIndex - static_cast<int>(recentBooks.size());
    if (idx < static_cast<int>(recentArticles.size())) {
      onSelectBook(recentArticles[idx].path);
      return;
    }

    // Then menu items
    int menuIdx = idx - static_cast<int>(recentArticles.size());
    int m = 0;
    const int fileBrowserIdx = m++;
    const int recentsIdx = m++;
    const int opdsLibraryIdx = hasOpdsUrl ? m++ : -1;
    const int fileTransferIdx = m++;
    const int settingsIdx = m;

    if (menuIdx == fileBrowserIdx) {
      onFileBrowserOpen();
    } else if (menuIdx == recentsIdx) {
      onRecentsOpen();
    } else if (menuIdx == opdsLibraryIdx) {
      onOpdsBrowserOpen();
    } else if (menuIdx == fileTransferIdx) {
      onFileTransferOpen();
    } else if (menuIdx == settingsIdx) {
      onSettingsOpen();
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  // Draw header
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, nullptr);

  // Content area below header
  const int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - (contentY + metrics.buttonHintsHeight + metrics.verticalSpacing);

  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  // Cover tile area for recent books
  GUI.drawRecentBookCover(renderer, Rect{0, contentY, pageWidth, metrics.homeCoverTileHeight}, recentBooks,
                          selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build combined menu: recent articles + static menu items
  // Max: 8 articles + 5 menu items (browse, recents, opds, transfer, settings) = 13
  static constexpr int MAX_MENU_ITEMS = 16;
  const char* allItems[MAX_MENU_ITEMS];
  UIIcon allIcons[MAX_MENU_ITEMS];
  int allCount = 0;

  for (const auto& article : recentArticles) {
    if (allCount >= MAX_MENU_ITEMS) break;
    allItems[allCount] = article.title.empty() ? article.path.c_str() : article.title.c_str();
    allIcons[allCount] = Book;
    allCount++;
  }

  const int articleMenuCount = allCount;

  // Static menu items
  auto addMenuItem = [&](const char* label, UIIcon icon) {
    if (allCount < MAX_MENU_ITEMS) {
      allItems[allCount] = label;
      allIcons[allCount] = icon;
      allCount++;
    }
  };
  addMenuItem(tr(STR_BROWSE_FILES), Folder);
  addMenuItem(tr(STR_MENU_RECENT_BOOKS), Recent);
  if (hasOpdsUrl) addMenuItem(tr(STR_OPDS_BROWSER), Library);
  addMenuItem(tr(STR_FILE_TRANSFER), Transfer);
  addMenuItem(tr(STR_SETTINGS_TITLE), Settings);

  const int menuY = contentY + metrics.homeCoverTileHeight + metrics.verticalSpacing;
  const int menuHeight = contentY + contentHeight - menuY;

  GUI.drawButtonMenu(
      renderer, Rect{0, menuY, pageWidth, menuHeight}, allCount, selectorIndex - static_cast<int>(recentBooks.size()),
      [&allItems](int index) { return std::string(allItems[index]); },
      [&allIcons](int index) { return allIcons[index]; });

  // Button hints
  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }
