/*
 * ============================================================================
 * 標頭檔引入說明
 * ============================================================================
 */

#include <stdio.h>   // 標準輸入輸出函式庫
                     // - fprintf(): 格式化輸出到 stderr，用於顯示使用方式
                     // - FILE*: 檔案指標型別，用於檔案操作
                     // - 實際實作 Huffman encoder 時還需要：
                     //   fopen(), fclose(), fread(), fwrite(), fgetc() 等

#include <stdlib.h>  // 標準函式庫
                     // - malloc(), free(): 動態記憶體配置
                     // - exit(), EXIT_SUCCESS, EXIT_FAILURE: 程式結束

#include <string.h>  // 處理字串：strcpy(), strlen(), memset(), 等
#include <math.h>    // 計算 entropy、perplexity 用到 log(), pow()

#include "logger.h"  // 自訂的 logger 函式庫（使用雙引號表示本地標頭檔）
                     // - log_info(): 記錄一般資訊（輸出到 stdout）
                     // - log_error(): 記錄錯誤訊息（輸出到 stderr）

/*
 * ============================================================================
 * Huffman Encoder 完整實作
 * ============================================================================
 * 
 * 【程式目的】
 * 讀取文字檔，統計 symbol 機率，建立 Huffman tree，
 * 產生 codebook.csv 與 encoded.bin，並輸出壓縮相關 metrics。
 * 
 * 【參數說明】
 * argv[1] - in_fn  : 輸入檔案路徑（待編碼的原始文字檔）
 * argv[2] - cb_fn  : codebook 輸出檔案路徑（儲存符號與編碼的對應表）
 * argv[3] - enc_fn : 編碼輸出檔案路徑（儲存編碼後的二進位資料）
 * 
 * 【執行範例】
 * ./encoder input.txt codebook.csv encoded.bin > encoder.log 2>&1
 * 
 * ============================================================================
 */

/* ============================================================================
 * Huffman Tree 節點定義
 * ==========================================================================*/

typedef struct Node {
    unsigned char symbol;        // 字元 (0~255)
    long          count;         // 出現次數
    double        prob;          // 機率
    struct Node  *left;
    struct Node  *right;
    char          code[256];     // Huffman code (0/1 字串)
} Node;

/* ----------------- min-heap for Huffman tree building -------------------- */

static Node* heap[512];
static int   heapSize = 0;

static void heap_push(Node* n) {
    heap[++heapSize] = n;
    int i = heapSize;
    while (i > 1 && heap[i]->count < heap[i/2]->count) {
        Node* tmp = heap[i];
        heap[i]   = heap[i/2];
        heap[i/2] = tmp;
        i /= 2;
    }
}

static Node* heap_pop(void) {
    if (heapSize == 0) return NULL;
    Node* root = heap[1];
    heap[1] = heap[heapSize--];

    int i = 1;
    while (1) {
        int left  = i * 2;
        int right = i * 2 + 1;
        int min_i = i;

        if (left <= heapSize && heap[left]->count < heap[min_i]->count)
            min_i = left;
        if (right <= heapSize && heap[right]->count < heap[min_i]->count)
            min_i = right;

        if (min_i == i) break;

        Node* tmp = heap[i];
        heap[i]   = heap[min_i];
        heap[min_i] = tmp;
        i = min_i;
    }
    return root;
}

/* --------------------- 產生 Huffman code 的遞迴函式 ----------------------- */

static void generate_codes(Node* root, const char* prefix) {
    if (!root) return;

    // 葉節點：指定 code
    if (!root->left && !root->right) {
        if (prefix[0] == '\0') {
            // 特例：只有一種 symbol，給它 "0"
            strcpy(root->code, "0");
        } else {
            strcpy(root->code, prefix);
        }
        return;
    }

    char left_prefix[256];
    char right_prefix[256];

    snprintf(left_prefix,  sizeof(left_prefix),  "%s0", prefix);
    snprintf(right_prefix, sizeof(right_prefix), "%s1", prefix);

    generate_codes(root->left,  left_prefix);
    generate_codes(root->right, right_prefix);
}

/* ----------------------------- 釋放樹的記憶體 ----------------------------- */

static void free_tree(Node* root) {
    if (!root) return;
    free_tree(root->left);
    free_tree(root->right);
    free(root);
}

/* ---------------------- codebook 排序用比較函式 --------------------------- */

static int compare_nodes(const void* a, const void* b) {
    const Node* na = *(const Node**)a;
    const Node* nb = *(const Node**)b;

    if (na->count != nb->count)
        return (na->count < nb->count) ? -1 : 1;
    if (na->symbol != nb->symbol)
        return (na->symbol < nb->symbol) ? -1 : 1;
    return 0;
}

/* ------------------------ 輸出 symbol 字串（含跳脫） ---------------------- */

static void print_symbol(FILE* fp, unsigned char s) {
    // 依 decoder 的 parse_symbol 寫法，\n, \t, \r 用跳脫字元
    if (s == '\n') {
        fprintf(fp, "\"\\n\"");
    } else if (s == '\t') {
        fprintf(fp, "\"\\t\"");
    } else if (s == '\r') {
        fprintf(fp, "\"\\r\"");
    } else if (s == '\"') {
        fprintf(fp, "\"\\\"\"");
    } else if (s == '\\') {
        fprintf(fp, "\"\\\\\"");
    } else {
        fprintf(fp, "\"%c\"", s);
    }
}

/* ============================================================================
 * 主程式
 * ==========================================================================*/

int main(int argc, char **argv) {
    /* ========================================================================
     * 步驟 1: 參數驗證
     * ======================================================================== */
    
    if (argc != 4) {
        log_error("encoder", "invalid_arguments argc=%d", argc);
        fprintf(stderr, "Usage: %s in_fn cb_fn enc_fn\n", argv[0]);
        return 1;
    }

    const char *in_fn  = argv[1];  // 輸入檔案（原始文字）
    const char *cb_fn  = argv[2];  // codebook 檔案（符號編碼表）
    const char *enc_fn = argv[3];  // 編碼輸出檔案（二進位資料）

    /* ========================================================================
     * 步驟 2: 記錄程式開始執行
     * ======================================================================== */
    
    log_info("encoder", "start input_file=%s cb_fn=%s enc_fn=%s",
             in_fn, cb_fn, enc_fn);

    /* ========================================================================
     * 步驟 3: Huffman 編碼主要邏輯
     * ======================================================================== */

    long freq[256] = {0};         // 每個 symbol 的出現次數
    long total_count = 0;         // 總符號數（包含重複）
    int  i, c;

    // 3-1. 讀取輸入檔案並統計頻率
    FILE *fin = fopen(in_fn, "rb");
    if (!fin) {
        log_error("encoder", "cannot_open_input_file file=%s", in_fn);
        log_info("encoder", "finish status=error");
        return 1;
    }

    while ((c = fgetc(fin)) != EOF) {
        freq[(unsigned char)c]++;
        total_count++;
    }
    fclose(fin);

    // 若輸入檔案是空的，輸出空 codebook 與空 encoded 檔即可
    if (total_count == 0) {
        log_info("encoder", "empty_input_file");
        FILE *fcb_empty = fopen(cb_fn, "w");
        if (fcb_empty) fclose(fcb_empty);
        FILE *fenc_empty = fopen(enc_fn, "wb");
        if (fenc_empty) fclose(fenc_empty);

        // metrics 全部為 0
        log_info("metrics",
                 "summary input_file=%s num_symbols=%ld "
                 "fixed_code_bits_per_symbol=%.15f "
                 "entropy_bits_per_symbol=%.15f "
                 "perplexity=%.15f "
                 "huffman_bits_per_symbol=%.15f "
                 "total_bits_fixed=%.15f "
                 "total_bits_huffman=%.15f "
                 "compression_ratio=%.15f "
                 "compression_factor=%.15f "
                 "saving_percentage=%.15f",
                 in_fn,
                 (long)0,
                 0.0, 0.0, 0.0, 0.0,
                 0.0, 0.0, 0.0, 0.0, 0.0);

        log_info("encoder", "finish status=ok");
        return 0;
    }

    // 3-2. 建立 Huffman tree：為每個有出現的 symbol 建 leaf node
    Node* leaf_nodes[256] = {0};
    int distinct_count = 0;
    heapSize = 0;

    for (i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            Node* n = (Node*)malloc(sizeof(Node));
            n->symbol = (unsigned char)i;
            n->count  = freq[i];
            n->prob   = 0.0;
            n->left   = n->right = NULL;
            n->code[0] = '\0';
            heap_push(n);
            leaf_nodes[i] = n;
            distinct_count++;
        }
    }

    // 合併成一棵 Huffman tree
    while (heapSize > 1) {
        Node* a = heap_pop();
        Node* b = heap_pop();
        Node* parent = (Node*)malloc(sizeof(Node));
        parent->symbol = 0;
        parent->count  = a->count + b->count;
        parent->prob   = 0.0;
        parent->left   = a;
        parent->right  = b;
        parent->code[0] = '\0';
        heap_push(parent);
    }

    Node* root = heap_pop();

    // 3-3. 產生每個 symbol 的 Huffman code
    generate_codes(root, "");

    // 3-4. 計算機率與自資訊、entropy、平均 code 長度
    Node* node_list[256];
    int   node_idx = 0;
    for (i = 0; i < 256; i++) {
        if (leaf_nodes[i]) {
            leaf_nodes[i]->prob = (double)leaf_nodes[i]->count /
                                  (double)total_count;
            node_list[node_idx++] = leaf_nodes[i];
        }
    }

    // 排序 codebook：依 count, symbol
    qsort(node_list, node_idx, sizeof(Node*), compare_nodes);

    double entropy = 0.0;
    double avg_code_len = 0.0;
    long   total_bits_huffman = 0;

    for (i = 0; i < node_idx; i++) {
        Node* n = node_list[i];
        double p = n->prob;
        double self_info = 0.0;
        if (p > 0.0) {
            self_info = -log(p) / log(2.0);  // log2(1/p) = -log2(p)
            entropy  += p * self_info;
        }
        int code_len = (int)strlen(n->code);
        avg_code_len += p * code_len;
        total_bits_huffman += (long)code_len * n->count;
    }

    double perplexity = pow(2.0, entropy);

    // 3-5. 輸出 codebook.csv
    FILE *fcb = fopen(cb_fn, "w");
    if (!fcb) {
        log_error("encoder", "cannot_open_codebook_output file=%s", cb_fn);
        log_info("encoder", "finish status=error");
        free_tree(root);
        return 1;
    }

    for (i = 0; i < node_idx; i++) {
        Node* n = node_list[i];
        double self_info = (n->prob > 0.0)
                           ? -log(n->prob) / log(2.0)
                           : 0.0;

        print_symbol(fcb, n->symbol);
        fprintf(fcb, ",%ld,%.15f,\"%s\",%.15f\n",
                n->count,
                n->prob,
                n->code,
                self_info);
    }
    fclose(fcb);

    // 3-6. 使用 Huffman code 編碼原始資料 → encoded.bin
    fin = fopen(in_fn, "rb");
    if (!fin) {
        log_error("encoder", "cannot_reopen_input_file file=%s", in_fn);
        log_info("encoder", "finish status=error");
        free_tree(root);
        return 1;
    }

    FILE *fenc = fopen(enc_fn, "wb");
    if (!fenc) {
        log_error("encoder", "cannot_open_encoded_output file=%s", enc_fn);
        log_info("encoder", "finish status=error");
        fclose(fin);
        free_tree(root);
        return 1;
    }

    unsigned char out_byte = 0;
    int bit_count = 0;
    while ((c = fgetc(fin)) != EOF) {
        Node* n = leaf_nodes[(unsigned char)c];
        const char* code = n->code;
        for (int k = 0; code[k] != '\0'; k++) {
            out_byte = (out_byte << 1) | (code[k] - '0');
            bit_count++;
            if (bit_count == 8) {
                fwrite(&out_byte, 1, 1, fenc);
                out_byte = 0;
                bit_count = 0;
            }
        }
    }
    fclose(fin);

    // 若最後不足 8 bits，用 0 padding
    if (bit_count > 0) {
        out_byte <<= (8 - bit_count);
        fwrite(&out_byte, 1, 1, fenc);
    }
    fclose(fenc);

    /* ========================================================================
     * 步驟 4: 計算並輸出 Metrics 統計資訊
     * ======================================================================== */

    // num_symbols：這裡我們用「總符號數（包含重複）」比較直覺
    long num_symbols = total_count;

    // fixed_bps: 固定長度編碼所需 bits per symbol
    // 使用不重複符號個數 distinct_count 來決定需要幾個 bit
    int fixed_bits = 0;
    int tmp = distinct_count - 1;
    while (tmp > 0) {
        fixed_bits++;
        tmp >>= 1;
    }
    if (fixed_bits == 0) fixed_bits = 1; // 至少 1 個 bit
    double fixed_bps = (double)fixed_bits;

    double entropy_bps        = entropy;               // bits per symbol
    double perplexity_val     = perplexity;
    double huffman_bps        = (double)total_bits_huffman /
                                (double)total_count;
    double total_bits_fixed   = (double)total_count * fixed_bps;
    double total_bits_huff_d  = (double)total_bits_huffman;
    double compression_ratio  = total_bits_fixed / total_bits_huff_d;
    double compression_factor = total_bits_huff_d / total_bits_fixed;
    double saving_percentage  = 1.0 - compression_factor;

    log_info("metrics",
             "summary input_file=%s num_symbols=%ld "
             "fixed_code_bits_per_symbol=%.15f "
             "entropy_bits_per_symbol=%.15f "
             "perplexity=%.15f "
             "huffman_bits_per_symbol=%.15f "
             "total_bits_fixed=%.15f "
             "total_bits_huffman=%.15f "
             "compression_ratio=%.15f "
             "compression_factor=%.15f "
             "saving_percentage=%.15f",
             in_fn,
             num_symbols,
             fixed_bps,
             entropy_bps,
             perplexity_val,
             huffman_bps,
             total_bits_fixed,
             total_bits_huff_d,
             compression_ratio,
             compression_factor,
             saving_percentage);

    /* ========================================================================
     * 步驟 5: 記錄程式成功結束
     * ======================================================================== */
    
    log_info("encoder", "finish status=ok");

    free_tree(root);
    return 0;
}
