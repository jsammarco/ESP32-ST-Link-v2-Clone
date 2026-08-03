#include <cstdio>
#include <cstring>

#include "raz_html_renderer.h"

namespace {

unsigned failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::printf("FAIL line %d: %s\n", __LINE__, #condition);                \
      ++failures;                                                               \
    }                                                                           \
  } while (0)

void feed_chunks(RazHtmlRenderer &renderer, const char *html, size_t chunk) {
  const size_t length = std::strlen(html);
  for (size_t offset = 0; offset < length; offset += chunk) {
    size_t count = length - offset;
    if (count > chunk) {
      count = chunk;
    }
    renderer.feed(reinterpret_cast<const uint8_t *>(html + offset), count);
  }
  renderer.finish();
}

void test_semantic_layout_and_entities() {
  RazHtmlRenderer renderer;
  feed_chunks(renderer,
              "<!doctype html><html><head><title>Small &amp; Safe</title>"
              "<style>.gone{display:none} h2{text-transform:uppercase}</style>"
              "<script>bad <b>script</b></script></head><body>"
              "<h2>Hello web</h2><p>A bounded &lt;page&gt; with text.</p>"
              "<div class='gone'>secret</div><ul><li>First</li><li>Second</li></ul>"
              "</body></html>",
              7U);
  CHECK(std::strcmp(renderer.title(), "Small & Safe") == 0);
  CHECK(renderer.line_count() == 4U);
  CHECK(std::strcmp(renderer.line_at(0)->text, "HELLO WEB") == 0);
  CHECK(renderer.line_at(0)->style == RAZ_WEB_HEADING);
  CHECK(std::strcmp(renderer.line_at(1)->text, "A bounded <page> with text.") == 0);
  CHECK(std::strcmp(renderer.line_at(2)->text, "- First") == 0);
  CHECK(renderer.line_at(2)->style == RAZ_WEB_LIST);
  CHECK(std::strstr(renderer.line_at(0)->text, "script") == nullptr);
}

void test_wrapping_and_inline_css() {
  RazHtmlRenderer renderer;
  feed_chunks(renderer,
              "<title>X</title><p style='font-weight:bold'>"
              "12345678901234567890123456789012345</p>"
              "<p style='display:none'>hidden</p><p>shown</p>",
              1U);
  CHECK(renderer.line_count() == 3U);
  CHECK(std::strlen(renderer.line_at(0)->text) == RAZ_WEB_LINE_CHARS);
  CHECK(renderer.line_at(0)->style == RAZ_WEB_HEADING);
  CHECK(std::strcmp(renderer.line_at(2)->text, "shown") == 0);
}

void test_document_bound() {
  RazHtmlRenderer renderer;
  static const char paragraph[] = "<p>bounded line</p>";
  for (size_t index = 0U; index < RAZ_WEB_MAX_LINES + 20U; ++index) {
    renderer.feed(reinterpret_cast<const uint8_t *>(paragraph),
                  sizeof(paragraph) - 1U);
  }
  renderer.finish();
  CHECK(renderer.line_count() == RAZ_WEB_MAX_LINES);
  CHECK(renderer.truncated());
  CHECK(renderer.line_at(RAZ_WEB_MAX_LINES) == nullptr);
}

}  // namespace

int main() {
  test_semantic_layout_and_entities();
  test_wrapping_and_inline_css();
  test_document_bound();
  if (failures != 0U) {
    std::printf("%u HTML renderer test(s) failed.\n", failures);
    return 1;
  }
  std::puts("All ESP32 HTML renderer host tests passed.");
  return 0;
}
