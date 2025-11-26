#include <stdio.h>   // 標準輸入輸出函式庫
#include <stdlib.h>  // 標準函式庫
#include <string.h>  // 處理字串用，例如 strchr(), sscanf()
#include "logger.h"  // 自訂 logger 函式庫

/*
 * ============================================================================
 * Huffman Decoder 完整實作
 * ============================================================================
 * 
 * 【程式目的】
 * 從 codebook.csv 重建 Huffman 解碼樹，讀取 encoded.bin 的 bit stream，
 * 逐位元走 Huffman tree 還原符號，輸出成文字檔 out_fn。
 * 
 * 【參數說明】
 * argv[1] - enc_fn : 編碼檔案路徑（由 encoder 產生的二進位檔案）
 * argv[2] - cb_fn  : codebook 檔案路徑（符號與編碼的對應表）
 * argv[3] - out_fn : 解碼輸出檔案路徑（還原後的原始文字檔）
 * 
 * 【Log 輸出規範】
 * - 使用 log_info() 記錄正常流程（輸出到 stdout）
 * - 使用 log_error() 記錄錯誤訊息（輸出到 stderr）
 * - 每行 log 包含：時間戳記、等級、元件名稱、訊息內容
 * 
 * ============================================================================
 */

/* ============================================================================
 * Huffman 解碼樹節點定義與相關函式
 * ==========================================================================*/

typedef struct DNode {
    char symbol;           // 葉節點存放的字元
    int  isLeaf;           // 1 = 葉節點, 0 = 內部節點
    struct DNode *left;
    struct DNode *right;
} DNode;

// 建立新節點
static DNode* create_node(void) {
    DNode* n = (DNode*)malloc(sizeof(DNode));
    if (!n) {
        // 這裡用 fprintf 是避免 logger 本身也出問題時陷入循環
        fprintf(stderr, "decoder: memory allocation failed\n");
        exit(1);
    }
    n->symbol = 0;
    n->isLeaf = 0;
    n->left = n->right = NULL;
    return n;
}

// 將一個 codeword 插入 Huffman 解碼樹中
static void insert_code(DNode* root, const char* code, char symbol) {
    DNode* cur = root;
    for (int i = 0; code[i] != '\0'; i++) {
        if (code[i] == '0') {
            if (!cur->left) cur->left = create_node();
            cur = cur->left;
        } else if (code[i] == '1') {
            if (!cur->right) cur->right = create_node();
            cur = cur->right;
        }
    }
    cur->isLeaf = 1;
    cur->symbol = symbol;
}

// 釋放整棵樹
static void free_tree(DNode* root) {
    if (!root) return;
    free_tree(root->left);
    free_tree(root->right);
    free(root);
}

/*
 * 從 codebook.csv 的一整行字串中解析出第一欄 symbol
 * 範例：
 *   "\"a\",1,0.021277000000000,\"00010\",5.55456..."
 *   "\"\\n\",1,0.0212...,\"00001\",5.55..."
 */
static char parse_symbol(const char* line) {
    if (line[0] != '"') return 0;

    if (line[1] == '\\') {
        // 處理跳脫字元：\n, \t, \r ...
        switch (line[2]) {
            case 'n': return '\n';
            case 't': return '\t';
            case 'r': return '\r';
            case '0': return '\0';
            default:  return line[2]; // 其他就直接回傳
        }
    } else {
        // 一般字元
        return line[1];
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
        log_error("decoder", "invalid_arguments argc=%d", argc);
        fprintf(stderr, "Usage: %s enc_fn cb_fn out_fn\n", argv[0]);
        return 1;
    }

    const char *enc_fn = argv[1];  // 編碼檔案（二進位資料）
    const char *cb_fn  = argv[2];  // codebook 檔案（符號編碼表）
    const char *out_fn = argv[3];  // 輸出檔案（還原的文字）

    /* ========================================================================
     * 步驟 2: 記錄程式開始執行
     * ======================================================================== */
    
    log_info("decoder",
             "start input_encoded=%s input_codebook=%s output_file=%s",
             enc_fn, cb_fn, out_fn);

    /* ========================================================================
     * 步驟 3: Huffman 解碼主要邏輯
     * ======================================================================== */

    int status_ok = 1;             // 解碼是否成功
    long num_decoded_symbols = 0;  // 實際解出多少個 symbol
    long expected_symbols = 0;     // 從 codebook 的 count 加總出來的符號總數

    // 3-1. 讀取 codebook，建立 Huffman 解碼樹
    FILE *fcb = fopen(cb_fn, "r");
    if (!fcb) {
        log_error("decoder", "cannot_open_codebook file=%s", cb_fn);
        log_info("decoder", "finish status=error");
        return 1;
    }

    DNode* root = create_node();

    char line[512];
    while (fgets(line, sizeof(line), fcb)) {
        // 解析 symbol（第一欄）
        char symbol = parse_symbol(line);

        // 找到第一欄結束的雙引號位置
        char *p = strchr(line + 1, '"');
        if (!p) continue; // 格式怪怪的就跳過
        p++;              // 指到雙引號後面
        if (*p == ',') p++; // 移到 count 的開頭

        long   count      = 0;
        double prob       = 0.0;
        char   code[256]  = {0};
        double self_info  = 0.0;

        // 剩下部分格式: count,prob,"code",self_info
        int n = sscanf(p, "%ld,%lf,\"%255[01]\",%lf",
                       &count, &prob, code, &self_info);
        if (n == 4) {
            insert_code(root, code, symbol);
            expected_symbols += count;
        }
    }
    fclose(fcb);

    // 3-2. 開啟 encoded.bin 與 output 檔案
    FILE *fenc = fopen(enc_fn, "rb");
    if (!fenc) {
        log_error("decoder", "cannot_open_encoded_file file=%s", enc_fn);
        log_info("decoder", "finish status=error");
        free_tree(root);
        return 1;
    }

    FILE *fout = fopen(out_fn, "w");
    if (!fout) {
        log_error("decoder", "cannot_open_output_file file=%s", out_fn);
        log_info("decoder", "finish status=error");
        fclose(fenc);
        free_tree(root);
        return 1;
    }

    // 3-3. 逐 bit 解碼
    DNode* cur = root;
    unsigned char byte;
    long bit_position = 0;   // 若有錯誤，可以記錄第幾個 bit 出事

    while (fread(&byte, 1, 1, fenc) == 1 && num_decoded_symbols < expected_symbols) {
        // 從最高位元開始 (bit 7 -> bit 0)
        for (int b = 7; b >= 0 && num_decoded_symbols < expected_symbols; b--) {
            int bit = (byte >> b) & 1;
            bit_position++;

            cur = (bit == 0) ? cur->left : cur->right;

            if (!cur) {
                // 代表 bit stream 中出現無法對應的路徑
                log_error("decoder",
                          "invalid_codeword bit_position=%ld reason=unexpected_prefix",
                          bit_position);
                status_ok = 0;
                log_info("decoder", "finish status=error");

                fclose(fenc);
                fclose(fout);
                free_tree(root);
                return 1;
            }

            if (cur->isLeaf) {
                // 找到一個完整 symbol，輸出到檔案
                fputc(cur->symbol, fout);
                num_decoded_symbols++;
                cur = root;  // 回到根節點，準備解下一個 symbol
            }
        }
    }

    fclose(fenc);
    fclose(fout);

    if (num_decoded_symbols != expected_symbols) {
        // 正常情況下應該完全對上，否則標記為錯誤
        status_ok = 0;
    }

    /* ========================================================================
     * 步驟 4: 輸出 Metrics 統計資訊
     * ======================================================================== */
    
    log_info("metrics",
             "summary input_encoded=%s input_codebook=%s output_file=%s "
             "num_decoded_symbols=%ld expected_symbols=%ld status=%s",
             enc_fn,
             cb_fn,
             out_fn,
             num_decoded_symbols,
             expected_symbols,
             status_ok ? "ok" : "error");

    /* ========================================================================
     * 步驟 5: 記錄程式結束
     * ======================================================================== */
    
    log_info("decoder", "finish status=%s", status_ok ? "ok" : "error");

    free_tree(root);
    return status_ok ? 0 : 1;
}
