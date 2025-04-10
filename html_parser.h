#ifndef HTML_PARSER_H
#define HTML_PARSER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_TAG_LEN 32
#define MAX_ATTR_NAME 32
#define MAX_ATTR_VALUE 256
#define MAX_TEXT_CONTENT 1024
#define MAX_TITLE_LEN 256
#define MAX_LINKS 50
#define MAX_URL_LEN 512

// Cấu trúc lưu thông tin về một thuộc tính của tag HTML
typedef struct {
    char name[MAX_ATTR_NAME];
    char value[MAX_ATTR_VALUE];
} HtmlAttribute;

// Cấu trúc lưu thông tin về một node trong HTML
typedef struct HtmlNode {
    char tag[MAX_TAG_LEN];
    HtmlAttribute *attributes;
    int num_attributes;
    char text_content[MAX_TEXT_CONTENT];
    struct HtmlNode *parent;
    struct HtmlNode *children;
    int num_children;
    struct HtmlNode *next_sibling;
} HtmlNode;

// Cấu trúc lưu các thông tin được trích xuất từ HTML
typedef struct {
    char title[MAX_TITLE_LEN];
    char links[MAX_LINKS][MAX_URL_LEN];
    int num_links;
    char formatted_content[MAX_TEXT_CONTENT * 4];  // Nội dung đã được định dạng
} ParsedHtml;

// Phân tích cú pháp HTML và trả về cấu trúc cây
HtmlNode* parse_html_to_tree(const char *html);

// Trích xuất thông tin từ cây HTML
void extract_html_info(HtmlNode *root, ParsedHtml *result);

// Tạo nội dung định dạng cho việc hiển thị
void format_html_content(HtmlNode *root, ParsedHtml *result);

// Giải phóng bộ nhớ của cây HTML
void free_html_tree(HtmlNode *node);

// Phân tích HTML thành thông tin đã định dạng (hàm wrapper)
int parse_html(const char *html, ParsedHtml *result);

// Tìm liên kết từ HTML đơn giản theo index
const char* get_link_by_index(const ParsedHtml *parsed_html, int index);

// Kiểm tra xem một href có phải là URL đầy đủ hay không
int is_absolute_url(const char *href);

// Thêm liên kết tương đối vào domain
void resolve_relative_url(const char *base_url, const char *href, char *result, int max_len);

#endif /* HTML_PARSER_H */ 