#include "raz_html_renderer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace {

bool text_equal(const char *left, const char *right) {
  return strcmp(left, right) == 0;
}

bool is_block_tag(const char *tag) {
  static const char *const tags[] = {
      "address", "article", "aside", "blockquote", "dd", "div", "dl",
      "dt", "footer", "header", "h1", "h2", "h3", "h4", "h5", "h6",
      "li", "main", "p", "pre", "section", "table", "tr"};
  for (const char *candidate : tags) {
    if (text_equal(tag, candidate)) {
      return true;
    }
  }
  return false;
}

bool is_void_tag(const char *tag) {
  return text_equal(tag, "br") || text_equal(tag, "hr") ||
         text_equal(tag, "img") || text_equal(tag, "input") ||
         text_equal(tag, "link") || text_equal(tag, "meta") ||
         text_equal(tag, "source") || text_equal(tag, "wbr");
}

bool is_hidden_tag(const char *tag) {
  return text_equal(tag, "canvas") || text_equal(tag, "iframe") ||
         text_equal(tag, "form") || text_equal(tag, "noscript") || text_equal(tag, "script") ||
         text_equal(tag, "svg") || text_equal(tag, "template");
}

bool is_heading_tag(const char *tag) {
  return tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && tag[2] == '\0';
}

bool contains_property(const char *declarations, const char *property,
                       const char *value) {
  const size_t property_length = strlen(property);
  const size_t value_length = strlen(value);
  const char *cursor = declarations;
  while ((cursor = strstr(cursor, property)) != nullptr) {
    const char *colon = cursor + property_length;
    while (*colon == ' ' || *colon == '\t') {
      ++colon;
    }
    if (*colon++ != ':') {
      cursor += property_length;
      continue;
    }
    while (*colon == ' ' || *colon == '\t') {
      ++colon;
    }
    if (strncmp(colon, value, value_length) == 0) {
      return true;
    }
    cursor += property_length;
  }
  return false;
}

uint8_t declaration_flags(const char *declarations) {
  uint8_t flags = 0U;
  if (contains_property(declarations, "display", "none") ||
      contains_property(declarations, "visibility", "hidden")) {
    flags |= 1U << 0;
  }
  if (contains_property(declarations, "text-transform", "uppercase")) {
    flags |= 1U << 1;
  }
  if (contains_property(declarations, "font-weight", "bold") ||
      contains_property(declarations, "font-weight", "700") ||
      contains_property(declarations, "font-weight", "800") ||
      contains_property(declarations, "font-weight", "900")) {
    flags |= 1U << 2;
  }
  if (contains_property(declarations, "display", "block")) {
    flags |= 1U << 3;
  }
  return flags;
}

void lowercase_ascii(char *text) {
  while (*text != '\0') {
    if (*text >= 'A' && *text <= 'Z') {
      *text = static_cast<char>(*text + ('a' - 'A'));
    }
    ++text;
  }
}

void trim_ascii(char *text) {
  char *start = text;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
    ++start;
  }
  if (start != text) {
    memmove(text, start, strlen(start) + 1U);
  }
  size_t length = strlen(text);
  while (length != 0U && (text[length - 1U] == ' ' || text[length - 1U] == '\t' ||
                          text[length - 1U] == '\r' || text[length - 1U] == '\n')) {
    text[--length] = '\0';
  }
}

bool class_list_contains(const char *classes, const char *candidate) {
  const size_t wanted = strlen(candidate);
  const char *cursor = classes;
  while (*cursor != '\0') {
    while (*cursor == ' ' || *cursor == '\t') {
      ++cursor;
    }
    const char *start = cursor;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
      ++cursor;
    }
    if (static_cast<size_t>(cursor - start) == wanted &&
        strncmp(start, candidate, wanted) == 0) {
      return true;
    }
  }
  return false;
}

void read_attribute(const char *tag_text, const char *name,
                    char *output, size_t output_size) {
  output[0] = '\0';
  const size_t name_length = strlen(name);
  const char *cursor = tag_text;
  while (*cursor != '\0') {
    while (*cursor != '\0' && isspace(static_cast<unsigned char>(*cursor))) {
      ++cursor;
    }
    const char *key = cursor;
    while (isalnum(static_cast<unsigned char>(*cursor)) || *cursor == '-' ||
           *cursor == '_') {
      ++cursor;
    }
    const size_t key_length = static_cast<size_t>(cursor - key);
    while (*cursor == ' ' || *cursor == '\t') {
      ++cursor;
    }
    if (*cursor != '=') {
      while (*cursor != '\0' && !isspace(static_cast<unsigned char>(*cursor))) {
        ++cursor;
      }
      continue;
    }
    ++cursor;
    while (*cursor == ' ' || *cursor == '\t') {
      ++cursor;
    }
    char quote = 0;
    if (*cursor == '\'' || *cursor == '"') {
      quote = *cursor++;
    }
    const char *value = cursor;
    if (quote != 0) {
      while (*cursor != '\0' && *cursor != quote) {
        ++cursor;
      }
    } else {
      while (*cursor != '\0' && !isspace(static_cast<unsigned char>(*cursor)) &&
             *cursor != '>') {
        ++cursor;
      }
    }
    if (key_length == name_length && strncmp(key, name, name_length) == 0) {
      size_t length = static_cast<size_t>(cursor - value);
      if (length >= output_size) {
        length = output_size - 1U;
      }
      memcpy(output, value, length);
      output[length] = '\0';
      lowercase_ascii(output);
      return;
    }
    if (quote != 0 && *cursor == quote) {
      ++cursor;
    }
  }
}

}  // namespace

RazHtmlRenderer::RazHtmlRenderer() { reset(); }

void RazHtmlRenderer::reset() {
  line_count_ = 0U;
  title_length_ = 0U;
  title_[0] = '\0';
  line_length_ = 0U;
  line_[0] = '\0';
  current_style_ = RAZ_WEB_TEXT;
  current_flags_ = 0U;
  current_hidden_ = false;
  pending_space_ = false;
  truncated_ = false;
  in_tag_ = false;
  in_entity_ = false;
  in_comment_ = false;
  in_style_ = false;
  in_title_ = false;
  tag_quote_ = 0;
  tag_length_ = 0U;
  entity_length_ = 0U;
  raw_tag_[0] = '\0';
  css_length_ = 0U;
  css_[0] = '\0';
  css_rule_count_ = 0U;
  stack_depth_ = 0U;
}

void RazHtmlRenderer::feed(const uint8_t *data, size_t size) {
  for (size_t index = 0U; index < size; ++index) {
    const uint8_t raw = data[index];
    if ((raw & 0xC0U) == 0x80U) {
      continue;
    }
    feed_byte(raw > 0x7EU ? '?' : static_cast<char>(raw));
  }
}

void RazHtmlRenderer::feed_byte(char value) {
  if (in_comment_) {
    if (value == '>' && tag_length_ >= 2U && tag_[tag_length_ - 1U] == '-' &&
        tag_[tag_length_ - 2U] == '-') {
      in_comment_ = false;
      tag_length_ = 0U;
    } else {
      if (tag_length_ < 2U) {
        tag_[tag_length_++] = value;
      } else {
        tag_[0] = tag_[1];
        tag_[1] = value;
      }
    }
    return;
  }

  if (in_tag_) {
    if (tag_quote_ != 0) {
      if (value == tag_quote_) {
        tag_quote_ = 0;
      }
    } else if (value == '\'' || value == '"') {
      tag_quote_ = value;
    } else if (value == '>') {
      tag_[tag_length_] = '\0';
      in_tag_ = false;
      if (tag_length_ >= 3U && tag_[0] == '!' && tag_[1] == '-' && tag_[2] == '-') {
        if (!(tag_length_ >= 5U && tag_[tag_length_ - 1U] == '-' &&
              tag_[tag_length_ - 2U] == '-')) {
          in_comment_ = true;
          tag_length_ = 0U;
        }
      } else {
        parse_tag();
      }
      tag_length_ = 0U;
      return;
    }
    if (tag_length_ < sizeof(tag_) - 1U) {
      tag_[tag_length_++] = value;
    }
    return;
  }

  if (in_entity_) {
    if (value == ';') {
      entity_[entity_length_] = '\0';
      append_entity();
      entity_length_ = 0U;
      in_entity_ = false;
    } else if (entity_length_ < sizeof(entity_) - 1U &&
               (isalnum(static_cast<unsigned char>(value)) || value == '#')) {
      entity_[entity_length_++] = value;
    } else {
      in_entity_ = false;
      entity_length_ = 0U;
      append_text('?');
      feed_byte(value);
    }
    return;
  }

  if (value == '<') {
    in_tag_ = true;
    tag_length_ = 0U;
    tag_quote_ = 0;
    return;
  }
  if (in_style_) {
    if (css_length_ < sizeof(css_) - 1U) {
      css_[css_length_++] = static_cast<char>(tolower(static_cast<unsigned char>(value)));
      css_[css_length_] = '\0';
    }
    return;
  }
  if (value == '&') {
    in_entity_ = true;
    entity_length_ = 0U;
    return;
  }
  append_text(value);
}

void RazHtmlRenderer::parse_tag() {
  char local[sizeof(tag_)];
  memcpy(local, tag_, tag_length_ + 1U);
  trim_ascii(local);
  if (local[0] == '\0' || local[0] == '!') {
    return;
  }

  bool closing = false;
  char *cursor = local;
  if (*cursor == '/') {
    closing = true;
    ++cursor;
    while (*cursor == ' ' || *cursor == '\t') {
      ++cursor;
    }
  }
  char name[12];
  size_t name_length = 0U;
  while ((isalnum(static_cast<unsigned char>(*cursor)) || *cursor == '-') &&
         name_length < sizeof(name) - 1U) {
    name[name_length++] = static_cast<char>(tolower(static_cast<unsigned char>(*cursor++)));
  }
  name[name_length] = '\0';
  if (name[0] == '\0') {
    return;
  }

  if (raw_tag_[0] != '\0') {
    if (closing && text_equal(name, raw_tag_)) {
      if (in_style_) {
        css_[css_length_] = '\0';
        parse_css();
        in_style_ = false;
      }
      raw_tag_[0] = '\0';
      pop_element(name);
    }
    return;
  }

  if (closing) {
    if (is_block_tag(name)) {
      flush_line();
    }
    if (text_equal(name, "title")) {
      in_title_ = false;
    }
    pop_element(name);
    return;
  }

  bool self_closing = false;
  size_t local_length = strlen(local);
  while (local_length != 0U && isspace(static_cast<unsigned char>(local[local_length - 1U]))) {
    --local_length;
  }
  if (local_length != 0U && local[local_length - 1U] == '/') {
    self_closing = true;
  }

  if (text_equal(name, "br") || text_equal(name, "hr")) {
    flush_line();
    return;
  }

  const bool block = is_block_tag(name);
  if (block) {
    start_block();
  }

  char classes[96];
  char inline_style[128];
  read_attribute(cursor, "class", classes, sizeof(classes));
  read_attribute(cursor, "style", inline_style, sizeof(inline_style));
  const uint8_t flags = static_cast<uint8_t>(current_flags_ |
      css_flags_for(name, classes, inline_style));

  RazWebLineStyle style = current_style_;
  if (is_heading_tag(name) || text_equal(name, "strong") || text_equal(name, "b") ||
      (flags & CSS_BOLD) != 0U) {
    style = RAZ_WEB_HEADING;
  } else if (text_equal(name, "li")) {
    style = RAZ_WEB_LIST;
  } else if (text_equal(name, "a")) {
    style = RAZ_WEB_LINK;
  } else if (text_equal(name, "small") || text_equal(name, "time")) {
    style = RAZ_WEB_MUTED;
  }

  const bool hidden = current_hidden_ || is_hidden_tag(name) ||
      (flags & CSS_HIDDEN) != 0U;
  push_element(name, style, flags, hidden, block || (flags & CSS_BLOCK) != 0U);

  if (text_equal(name, "li") && !current_hidden_) {
    append_text('-');
    append_text(' ');
  }
  if (text_equal(name, "title")) {
    in_title_ = true;
  }
  if (text_equal(name, "style")) {
    in_style_ = true;
    css_length_ = 0U;
    css_[0] = '\0';
    strncpy(raw_tag_, "style", sizeof(raw_tag_) - 1U);
    raw_tag_[sizeof(raw_tag_) - 1U] = '\0';
  } else if (is_hidden_tag(name)) {
    strncpy(raw_tag_, name, sizeof(raw_tag_) - 1U);
    raw_tag_[sizeof(raw_tag_) - 1U] = '\0';
  }

  if (self_closing || is_void_tag(name)) {
    pop_element(name);
  }
}

void RazHtmlRenderer::push_element(const char *name, RazWebLineStyle style,
                                   uint8_t flags, bool hidden, bool block) {
  if (stack_depth_ >= sizeof(stack_) / sizeof(stack_[0])) {
    return;
  }
  Element &element = stack_[stack_depth_++];
  strncpy(element.name, name, sizeof(element.name) - 1U);
  element.name[sizeof(element.name) - 1U] = '\0';
  element.previous_style = current_style_;
  element.previous_flags = current_flags_;
  element.previous_hidden = current_hidden_;
  element.block = block;
  current_style_ = style;
  current_flags_ = flags;
  current_hidden_ = hidden;
}

void RazHtmlRenderer::pop_element(const char *name) {
  for (size_t depth = stack_depth_; depth != 0U; --depth) {
    if (!text_equal(stack_[depth - 1U].name, name)) {
      continue;
    }
    const Element element = stack_[depth - 1U];
    stack_depth_ = depth - 1U;
    current_style_ = element.previous_style;
    current_flags_ = element.previous_flags;
    current_hidden_ = element.previous_hidden;
    return;
  }
}

void RazHtmlRenderer::append_text(char value) {
  if (current_hidden_) {
    return;
  }
  if (in_title_) {
    append_title(value);
    return;
  }
  if (isspace(static_cast<unsigned char>(value))) {
    pending_space_ = line_length_ != 0U;
    return;
  }
  if (value < 0x20 || value > 0x7E) {
    value = '?';
  }
  if ((current_flags_ & CSS_UPPERCASE) != 0U && value >= 'a' && value <= 'z') {
    value = static_cast<char>(value - ('a' - 'A'));
  }
  if (pending_space_) {
    if (line_length_ >= RAZ_WEB_LINE_CHARS) {
      flush_line();
    } else if (line_length_ != 0U) {
      line_[line_length_++] = ' ';
    }
    pending_space_ = false;
  }
  if (line_length_ >= RAZ_WEB_LINE_CHARS) {
    flush_line();
  }
  if (line_length_ == 0U) {
    current_style_ = current_style_;
  }
  line_[line_length_++] = value;
  line_[line_length_] = '\0';
}

void RazHtmlRenderer::append_title(char value) {
  if (isspace(static_cast<unsigned char>(value))) {
    if (title_length_ != 0U && title_[title_length_ - 1U] != ' ' &&
        title_length_ < RAZ_WEB_TITLE_CHARS) {
      title_[title_length_++] = ' ';
    }
    return;
  }
  if (value < 0x20 || value > 0x7E) {
    value = '?';
  }
  if (title_length_ < RAZ_WEB_TITLE_CHARS) {
    title_[title_length_++] = value;
    title_[title_length_] = '\0';
  }
}

void RazHtmlRenderer::append_entity() {
  char value = '?';
  if (text_equal(entity_, "amp")) value = '&';
  else if (text_equal(entity_, "lt")) value = '<';
  else if (text_equal(entity_, "gt")) value = '>';
  else if (text_equal(entity_, "quot")) value = '"';
  else if (text_equal(entity_, "apos") || text_equal(entity_, "#39")) value = '\'';
  else if (text_equal(entity_, "nbsp")) value = ' ';
  else if (entity_[0] == '#') {
    char *end = nullptr;
    const long numeric = strtol(entity_ + 1, &end, 10);
    if (end != entity_ + 1 && *end == '\0' && numeric >= 0x20L && numeric <= 0x7EL) {
      value = static_cast<char>(numeric);
    }
  }
  append_text(value);
}

void RazHtmlRenderer::flush_line() {
  pending_space_ = false;
  while (line_length_ != 0U && line_[line_length_ - 1U] == ' ') {
    --line_length_;
  }
  if (line_length_ == 0U) {
    line_[0] = '\0';
    return;
  }
  if (line_count_ >= RAZ_WEB_MAX_LINES) {
    truncated_ = true;
    line_length_ = 0U;
    line_[0] = '\0';
    return;
  }
  RazWebLine &output = lines_[line_count_++];
  memcpy(output.text, line_, line_length_);
  output.text[line_length_] = '\0';
  output.style = current_style_;
  line_length_ = 0U;
  line_[0] = '\0';
}

void RazHtmlRenderer::start_block() {
  flush_line();
}

void RazHtmlRenderer::parse_css() {
  char *cursor = css_;
  while (*cursor != '\0') {
    char *open = strchr(cursor, '{');
    if (open == nullptr) {
      break;
    }
    char *close = strchr(open + 1, '}');
    if (close == nullptr) {
      break;
    }
    *open = '\0';
    *close = '\0';
    const uint8_t flags = declaration_flags(open + 1);
    char *selector = cursor;
    while (selector != nullptr && *selector != '\0') {
      char *comma = strchr(selector, ',');
      if (comma != nullptr) {
        *comma = '\0';
      }
      trim_ascii(selector);
      if (strchr(selector, ' ') == nullptr && strchr(selector, '>') == nullptr &&
          strchr(selector, ':') == nullptr && strchr(selector, '[') == nullptr) {
        add_css_rule(selector, flags);
      }
      selector = comma == nullptr ? nullptr : comma + 1;
    }
    cursor = close + 1;
  }
}

void RazHtmlRenderer::add_css_rule(const char *selector, uint8_t flags) {
  if (flags == 0U || selector[0] == '\0' ||
      css_rule_count_ >= sizeof(css_rules_) / sizeof(css_rules_[0])) {
    return;
  }
  CssRule &rule = css_rules_[css_rule_count_++];
  strncpy(rule.selector, selector, sizeof(rule.selector) - 1U);
  rule.selector[sizeof(rule.selector) - 1U] = '\0';
  rule.flags = flags;
}

uint8_t RazHtmlRenderer::css_flags_for(const char *tag, const char *classes,
                                       const char *inline_style) const {
  uint8_t flags = declaration_flags(inline_style);
  for (size_t index = 0U; index < css_rule_count_; ++index) {
    const CssRule &rule = css_rules_[index];
    if (rule.selector[0] == '.') {
      if (class_list_contains(classes, rule.selector + 1)) {
        flags = static_cast<uint8_t>(flags | rule.flags);
      }
    } else if (text_equal(rule.selector, tag)) {
      flags = static_cast<uint8_t>(flags | rule.flags);
    }
  }
  return flags;
}

void RazHtmlRenderer::finish() {
  if (in_entity_) {
    append_text('?');
    in_entity_ = false;
  }
  flush_line();
  while (title_length_ != 0U && title_[title_length_ - 1U] == ' ') {
    title_[--title_length_] = '\0';
  }
  if (title_length_ == 0U) {
    strncpy(title_, "WEB PAGE", sizeof(title_) - 1U);
    title_[sizeof(title_) - 1U] = '\0';
    title_length_ = strlen(title_);
  }
}

size_t RazHtmlRenderer::line_count() const { return line_count_; }

const RazWebLine *RazHtmlRenderer::line_at(size_t index) const {
  return index < line_count_ ? &lines_[index] : nullptr;
}

const char *RazHtmlRenderer::title() const { return title_; }

bool RazHtmlRenderer::truncated() const { return truncated_; }
