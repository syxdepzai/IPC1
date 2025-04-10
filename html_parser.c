#include "html_parser.h"

// Hàm trợ giúp loại bỏ khoảng trắng ở đầu và cuối chuỗi
char* trim(char* str) {
    if (!str) return NULL;
    
    // Trim leading space
    while(isspace((unsigned char)*str)) str++;
    
    if(*str == 0) return str;
    
    // Trim trailing space
    char* end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    
    // Write new null terminator
    *(end+1) = 0;
    
    return str;
}

// Hàm để cấp phát và khởi tạo một node HTML mới
HtmlNode* create_html_node(const char* tag_name) {
    HtmlNode* node = (HtmlNode*)malloc(sizeof(HtmlNode));
    if (!node) return NULL;
    
    memset(node, 0, sizeof(HtmlNode));
    strncpy(node->tag, tag_name ? tag_name : "", MAX_TAG_LEN - 1);
    node->tag[MAX_TAG_LEN - 1] = '\0';
    
    return node;
}

// Parser HTML đơn giản - chỉ bắt các tag và text cơ bản
HtmlNode* parse_html_to_tree(const char* html) {
    if (!html) return NULL;
    
    HtmlNode* root = create_html_node("root");
    HtmlNode* current = root;
    
    const char* ptr = html;
    while (*ptr) {
        if (*ptr == '<') {
            // Xử lý thẻ đóng
            if (*(ptr + 1) == '/') {
                ptr += 2;
                char tag_name[MAX_TAG_LEN] = {0};
                int i = 0;
                while (*ptr && *ptr != '>' && i < MAX_TAG_LEN - 1) {
                    tag_name[i++] = *ptr++;
                }
                tag_name[i] = '\0';
                
                // Trở về node cha nếu tag khớp
                if (current->parent && strcmp(current->tag, tag_name) == 0) {
                    current = current->parent;
                }
                
                while (*ptr && *ptr != '>') ptr++;
                if (*ptr) ptr++;
            }
            // Xử lý thẻ mở
            else {
                ptr++;
                char tag_name[MAX_TAG_LEN] = {0};
                int i = 0;
                
                // Lấy tên tag
                while (*ptr && !isspace((unsigned char)*ptr) && *ptr != '>' && *ptr != '/' && i < MAX_TAG_LEN - 1) {
                    tag_name[i++] = *ptr++;
                }
                tag_name[i] = '\0';
                
                // Tạo node mới
                HtmlNode* new_node = create_html_node(tag_name);
                new_node->parent = current;
                
                // Xử lý các thuộc tính (đơn giản)
                while (*ptr && *ptr != '>' && *ptr != '/') {
                    while (*ptr && isspace((unsigned char)*ptr)) ptr++;
                    
                    // Nếu gặp dấu > hoặc / thì dừng lại
                    if (*ptr == '>' || *ptr == '/') break;
                    
                    // Đọc tên thuộc tính
                    char attr_name[MAX_ATTR_NAME] = {0};
                    i = 0;
                    while (*ptr && *ptr != '=' && *ptr != '>' && *ptr != '/' && i < MAX_ATTR_NAME - 1) {
                        attr_name[i++] = *ptr++;
                    }
                    attr_name[i] = '\0';
                    
                    // Đọc giá trị thuộc tính
                    char attr_value[MAX_ATTR_VALUE] = {0};
                    if (*ptr == '=') {
                        ptr++;
                        if (*ptr == '"' || *ptr == '\'') {
                            char quote = *ptr;
                            ptr++;
                            i = 0;
                            while (*ptr && *ptr != quote && i < MAX_ATTR_VALUE - 1) {
                                attr_value[i++] = *ptr++;
                            }
                            attr_value[i] = '\0';
                            if (*ptr) ptr++;
                        }
                    }
                    
                    // Thêm thuộc tính vào node nếu có tên
                    if (attr_name[0]) {
                        new_node->attributes = realloc(
                            new_node->attributes, 
                            (new_node->num_attributes + 1) * sizeof(HtmlAttribute)
                        );
                        
                        if (new_node->attributes) {
                            strncpy(new_node->attributes[new_node->num_attributes].name, 
                                    attr_name, MAX_ATTR_NAME - 1);
                            new_node->attributes[new_node->num_attributes].name[MAX_ATTR_NAME - 1] = '\0';
                            
                            strncpy(new_node->attributes[new_node->num_attributes].value, 
                                    attr_value, MAX_ATTR_VALUE - 1);
                            new_node->attributes[new_node->num_attributes].value[MAX_ATTR_VALUE - 1] = '\0';
                            
                            new_node->num_attributes++;
                        }
                    }
                }
                
                // Thêm node mới vào node hiện tại
                if (current->num_children == 0) {
                    current->children = new_node;
                } else {
                    HtmlNode* sibling = current->children;
                    while (sibling->next_sibling) {
                        sibling = sibling->next_sibling;
                    }
                    sibling->next_sibling = new_node;
                }
                current->num_children++;
                
                // Kiểm tra xem có phải là self-closing tag không
                if (*ptr == '/') {
                    ptr++;
                    while (*ptr && *ptr != '>') ptr++;
                    if (*ptr) ptr++;
                } else {
                    if (*ptr == '>') ptr++;
                    
                    // Đối với một số thẻ đặc biệt, chúng ta luôn coi chúng là self-closing
                    if (strcmp(tag_name, "img") != 0 && 
                        strcmp(tag_name, "br") != 0 && 
                        strcmp(tag_name, "hr") != 0 && 
                        strcmp(tag_name, "meta") != 0 && 
                        strcmp(tag_name, "link") != 0 &&
                        strcmp(tag_name, "input") != 0) {
                        current = new_node;
                    }
                }
            }
        }
        // Xử lý text content
        else {
            const char* text_start = ptr;
            while (*ptr && *ptr != '<') ptr++;
            
            int text_len = ptr - text_start;
            if (text_len > 0) {
                char* text = (char*)malloc(text_len + 1);
                if (text) {
                    strncpy(text, text_start, text_len);
                    text[text_len] = '\0';
                    
                    // Chỉ thêm nội dung text nếu không phải là khoảng trắng
                    char* trimmed = trim(text);
                    if (trimmed && *trimmed) {
                        strncat(current->text_content, trimmed, 
                                MAX_TEXT_CONTENT - strlen(current->text_content) - 1);
                    }
                    
                    free(text);
                }
            }
        }
    }
    
    return root;
}

// Giải phóng bộ nhớ của cây HTML
void free_html_tree(HtmlNode* node) {
    if (!node) return;
    
    // Giải phóng các thuộc tính
    if (node->attributes) {
        free(node->attributes);
    }
    
    // Giải phóng các node con
    HtmlNode* child = node->children;
    while (child) {
        HtmlNode* next = child->next_sibling;
        free_html_tree(child);
        child = next;
    }
    
    free(node);
}

// Kiểm tra xem href có phải là URL tuyệt đối hay không
int is_absolute_url(const char* href) {
    return strstr(href, "://") != NULL || 
           strncmp(href, "//", 2) == 0 ||
           strncmp(href, "mailto:", 7) == 0 ||
           strncmp(href, "tel:", 4) == 0;
}

// Giải quyết URL tương đối
void resolve_relative_url(const char* base_url, const char* href, char* result, int max_len) {
    if (is_absolute_url(href)) {
        strncpy(result, href, max_len - 1);
        result[max_len - 1] = '\0';
        return;
    }
    
    // Xử lý href bắt đầu bằng '/'
    if (href[0] == '/') {
        // Tìm domain từ base_url
        const char* proto_end = strstr(base_url, "://");
        if (proto_end) {
            proto_end += 3;  // Bỏ qua "://"
            const char* path_start = strchr(proto_end, '/');
            if (path_start) {
                // Copy phần domain
                int domain_len = path_start - base_url;
                strncpy(result, base_url, domain_len);
                result[domain_len] = '\0';
                
                // Thêm href
                strncat(result, href, max_len - domain_len - 1);
            } else {
                // Không có path trong base_url
                snprintf(result, max_len, "%s%s", base_url, href);
            }
        } else {
            // Không tìm thấy "://" trong base_url
            snprintf(result, max_len, "%s%s", base_url, href);
        }
    } else {
        // href là path tương đối
        // Tìm thư mục cuối cùng trong base_url
        const char* last_slash = strrchr(base_url, '/');
        if (last_slash && last_slash != base_url + strlen(base_url) - 1) {
            // Copy base_url đến thư mục cha
            int dir_len = last_slash - base_url + 1;  // Bao gồm cả '/'
            strncpy(result, base_url, dir_len);
            result[dir_len] = '\0';
            
            // Thêm href
            strncat(result, href, max_len - dir_len - 1);
        } else {
            // Thêm '/' nếu base_url không kết thúc bằng '/'
            if (last_slash == NULL || last_slash != base_url + strlen(base_url) - 1) {
                snprintf(result, max_len, "%s/%s", base_url, href);
            } else {
                snprintf(result, max_len, "%s%s", base_url, href);
            }
        }
    }
}

// Trích xuất thông tin từ cây HTML
void extract_html_info(HtmlNode* root, ParsedHtml* result) {
    if (!root || !result) return;
    
    // Khởi tạo result
    memset(result, 0, sizeof(ParsedHtml));
    
    // Tìm title và links đệ quy
    void extract_recursive(HtmlNode* node, ParsedHtml* result, int depth) {
        if (!node) return;
        
        // Tìm title
        if (strcmp(node->tag, "title") == 0 && node->text_content[0] && result->title[0] == '\0') {
            strncpy(result->title, node->text_content, MAX_TITLE_LEN - 1);
            result->title[MAX_TITLE_LEN - 1] = '\0';
        }
        
        // Tìm links
        if (strcmp(node->tag, "a") == 0) {
            for (int i = 0; i < node->num_attributes; i++) {
                if (strcmp(node->attributes[i].name, "href") == 0 && 
                    result->num_links < MAX_LINKS) {
                    
                    strncpy(result->links[result->num_links], 
                            node->attributes[i].value, 
                            MAX_URL_LEN - 1);
                    result->links[result->num_links][MAX_URL_LEN - 1] = '\0';
                    result->num_links++;
                    break;
                }
            }
        }
        
        // Đệ quy với các node con
        HtmlNode* child = node->children;
        while (child) {
            extract_recursive(child, result, depth + 1);
            child = child->next_sibling;
        }
    }
    
    extract_recursive(root, result, 0);
}

// Định dạng HTML để hiển thị
void format_html_content(HtmlNode* root, ParsedHtml* result) {
    if (!root || !result) return;
    
    result->formatted_content[0] = '\0';
    
    void format_recursive(HtmlNode* node, ParsedHtml* result, int depth) {
        if (!node) return;
        
        // Định dạng theo loại thẻ
        if (strcmp(node->tag, "h1") == 0 || 
            strcmp(node->tag, "h2") == 0 || 
            strcmp(node->tag, "h3") == 0) {
            
            // Heading: Hiển thị đậm và có dòng mới
            if (node->text_content[0]) {
                char* trimmed = trim(node->text_content);
                if (trimmed && *trimmed) {
                    char heading[MAX_TEXT_CONTENT];
                    snprintf(heading, sizeof(heading), "\n%s%s%s\n", 
                             strcmp(node->tag, "h1") == 0 ? "= " : "== ",
                             trimmed,
                             strcmp(node->tag, "h1") == 0 ? " =" : " ==");
                    
                    strncat(result->formatted_content, heading, 
                            sizeof(result->formatted_content) - strlen(result->formatted_content) - 1);
                }
            }
        }
        else if (strcmp(node->tag, "p") == 0) {
            // Paragraph: Thêm dòng mới trước và sau
            if (node->text_content[0]) {
                char* trimmed = trim(node->text_content);
                if (trimmed && *trimmed) {
                    char para[MAX_TEXT_CONTENT];
                    snprintf(para, sizeof(para), "\n%s\n", trimmed);
                    
                    strncat(result->formatted_content, para, 
                            sizeof(result->formatted_content) - strlen(result->formatted_content) - 1);
                }
            }
        }
        else if (strcmp(node->tag, "a") == 0) {
            // Liên kết: [nội dung](URL)
            if (node->text_content[0]) {
                char* trimmed = trim(node->text_content);
                if (trimmed && *trimmed) {
                    char* href = NULL;
                    for (int i = 0; i < node->num_attributes; i++) {
                        if (strcmp(node->attributes[i].name, "href") == 0) {
                            href = node->attributes[i].value;
                            break;
                        }
                    }
                    
                    char link[MAX_TEXT_CONTENT];
                    if (href) {
                        snprintf(link, sizeof(link), "[%s](%s)", trimmed, href);
                    } else {
                        snprintf(link, sizeof(link), "%s", trimmed);
                    }
                    
                    strncat(result->formatted_content, link, 
                            sizeof(result->formatted_content) - strlen(result->formatted_content) - 1);
                }
            }
        }
        else if (strcmp(node->tag, "img") == 0) {
            // Hình ảnh: [alt text]
            char* alt = NULL;
            for (int i = 0; i < node->num_attributes; i++) {
                if (strcmp(node->attributes[i].name, "alt") == 0) {
                    alt = node->attributes[i].value;
                    break;
                }
            }
            
            if (alt && *alt) {
                char img[MAX_TEXT_CONTENT];
                snprintf(img, sizeof(img), "[IMAGE: %s]", alt);
                
                strncat(result->formatted_content, img, 
                        sizeof(result->formatted_content) - strlen(result->formatted_content) - 1);
            } else {
                strncat(result->formatted_content, "[IMAGE]", 
                        sizeof(result->formatted_content) - strlen(result->formatted_content) - 1);
            }
        }
        else if (strcmp(node->tag, "br") == 0) {
            // Line break
            strncat(result->formatted_content, "\n", 
                    sizeof(result->formatted_content) - strlen(result->formatted_content) - 1);
        }
        else if (strcmp(node->tag, "hr") == 0) {
            // Horizontal rule
            strncat(result->formatted_content, "\n-----------------------\n", 
                    sizeof(result->formatted_content) - strlen(result->formatted_content) - 1);
        }
        else if (strcmp(node->tag, "ul") == 0 || strcmp(node->tag, "ol") == 0) {
            // Danh sách: Chỉ thêm dòng mới trước danh sách
            strncat(result->formatted_content, "\n", 
                    sizeof(result->formatted_content) - strlen(result->formatted_content) - 1);
        }
        else if (strcmp(node->tag, "li") == 0) {
            // Mục trong danh sách: Thêm ký tự '- ' trước mỗi mục
            if (node->text_content[0]) {
                char* trimmed = trim(node->text_content);
                if (trimmed && *trimmed) {
                    char list_item[MAX_TEXT_CONTENT];
                    snprintf(list_item, sizeof(list_item), "- %s\n", trimmed);
                    
                    strncat(result->formatted_content, list_item, 
                            sizeof(result->formatted_content) - strlen(result->formatted_content) - 1);
                }
            }
        }
        else {
            // Các thẻ khác: Chỉ lấy nội dung text
            if (node->text_content[0]) {
                char* trimmed = trim(node->text_content);
                if (trimmed && *trimmed) {
                    strncat(result->formatted_content, trimmed, 
                            sizeof(result->formatted_content) - strlen(result->formatted_content) - 1);
                    
                    // Thêm khoảng trắng nếu không phải là dấu câu
                    if (result->formatted_content[strlen(result->formatted_content) - 1] != ' ' &&
                        result->formatted_content[strlen(result->formatted_content) - 1] != '\n') {
                        strncat(result->formatted_content, " ", 
                                sizeof(result->formatted_content) - strlen(result->formatted_content) - 1);
                    }
                }
            }
        }
        
        // Đệ quy với các node con
        HtmlNode* child = node->children;
        while (child) {
            format_recursive(child, result, depth + 1);
            child = child->next_sibling;
        }
    }
    
    format_recursive(root, result, 0);
    
    // Cắt bỏ khoảng trắng ở cuối
    int len = strlen(result->formatted_content);
    while (len > 0 && isspace((unsigned char)result->formatted_content[len - 1])) {
        result->formatted_content[--len] = '\0';
    }
}

// Hàm wrapper để phân tích HTML
int parse_html(const char* html, ParsedHtml* result) {
    if (!html || !result) return 0;
    
    HtmlNode* root = parse_html_to_tree(html);
    if (!root) return 0;
    
    extract_html_info(root, result);
    format_html_content(root, result);
    
    free_html_tree(root);
    return 1;
}

// Lấy liên kết bằng index
const char* get_link_by_index(const ParsedHtml* parsed_html, int index) {
    if (!parsed_html || index < 0 || index >= parsed_html->num_links) {
        return NULL;
    }
    
    return parsed_html->links[index];
} 