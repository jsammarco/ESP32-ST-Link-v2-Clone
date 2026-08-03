#ifndef RAZ_HTML_RENDERER_H
#define RAZ_HTML_RENDERER_H

#include <stddef.h>
#include <stdint.h>

constexpr size_t RAZ_WEB_LINE_CHARS = 29U;
constexpr size_t RAZ_WEB_MAX_LINES = 160U;
constexpr size_t RAZ_WEB_TITLE_CHARS = 39U;

enum RazWebLineStyle : uint8_t {
  RAZ_WEB_TEXT = 0,
  RAZ_WEB_HEADING,
  RAZ_WEB_LIST,
  RAZ_WEB_LINK,
  RAZ_WEB_MUTED,
};

struct RazWebLine {
  char text[RAZ_WEB_LINE_CHARS + 1U];
  RazWebLineStyle style;
};

class RazHtmlRenderer {
 public:
  RazHtmlRenderer();

  void reset();
  void feed(const uint8_t *data, size_t size);
  void finish();

  size_t line_count() const;
  const RazWebLine *line_at(size_t index) const;
  const char *title() const;
  bool truncated() const;

 private:
  enum : uint8_t {
    CSS_HIDDEN = 1U << 0,
    CSS_UPPERCASE = 1U << 1,
    CSS_BOLD = 1U << 2,
    CSS_BLOCK = 1U << 3,
  };

  struct CssRule {
    char selector[24];
    uint8_t flags;
  };

  struct Element {
    char name[12];
    RazWebLineStyle previous_style;
    uint8_t previous_flags;
    bool previous_hidden;
    bool block;
  };

  void feed_byte(char value);
  void parse_tag();
  void parse_css();
  void push_element(const char *name, RazWebLineStyle style, uint8_t flags,
                    bool hidden, bool block);
  void pop_element(const char *name);
  void append_text(char value);
  void append_title(char value);
  void append_entity();
  void flush_line();
  void start_block();
  void add_css_rule(const char *selector, uint8_t flags);
  uint8_t css_flags_for(const char *tag, const char *classes,
                        const char *inline_style) const;

  RazWebLine lines_[RAZ_WEB_MAX_LINES];
  size_t line_count_;
  char title_[RAZ_WEB_TITLE_CHARS + 1U];
  size_t title_length_;
  char line_[RAZ_WEB_LINE_CHARS + 1U];
  size_t line_length_;
  RazWebLineStyle current_style_;
  uint8_t current_flags_;
  bool current_hidden_;
  bool pending_space_;
  bool truncated_;

  bool in_tag_;
  bool in_entity_;
  bool in_comment_;
  bool in_style_;
  bool in_title_;
  char tag_quote_;
  char tag_[192];
  size_t tag_length_;
  char entity_[12];
  size_t entity_length_;
  char raw_tag_[12];

  char css_[2048];
  size_t css_length_;
  CssRule css_rules_[20];
  size_t css_rule_count_;
  Element stack_[20];
  size_t stack_depth_;
};

#endif
