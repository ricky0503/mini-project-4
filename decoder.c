#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "logger.h"

// -----------------------------
// Huffman 解碼樹的節點
// -----------------------------
typedef struct DNode {
    char symbol;            // 葉節點存放字元
    int isLeaf;             // 1 = 葉節點, 0 = 內部節點
    struct DNode *left;
    struct DNode *right;
} DNode;



// 建立新節點
DNode* create_node() {
    DNode* n = (DNode*)malloc(sizeof(DNode));
    n->symbol = 0;
    n->isLeaf = 0;
    n->left = n->right = NULL;
    return n;
}

// 把一個 codeword 插入 Huffman 解碼樹
void insert_code(DNode* root, const char* code, char symbol) {
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

// 釋放整棵樹的記憶體（加分用，可不一定要）
void free_tree(DNode* root) {
    if (!root) return;
    free_tree(root->left);
    free_tree(root->right);
    free(root);
}

// 解析 codebook.csv 第一欄的 "symbol"
static char parse_symbol(const char* line) {
    // line 的開頭形如:
    // "a",1,0.0,"010",5.0
    // 或 "\n",1,0.0,"010",5.0
    if (line[0] != '"') return 0;
    if (line[1] == '\\') {
        // 處理跳脫字元
        if (line[2] == 'n') return '\n';
        if (line[2] == 't') return '\t';
        if (line[2] == 'r') return '\r';
        // 其他就直接回傳第二個字元
        return line[2];
    } else {
        // 一般字元
        return line[1];
    }
}

int main(int argc, char* argv[]) {

    if (argc != 4) {
        printf("Usage: ./decoder.exe output.txt codebook.csv encoded.bin\n");
        return 1;
    }

    char* out_fn = argv[1];   // 輸出解碼後文字檔
    char* cb_fn  = argv[2];   // 輸入 codebook.csv
    char* enc_fn = argv[3];   // 輸入 encoded.bin

    // --------- log: start ---------
    log_info("decoder",
             "start input_encoded=%s input_codebook=%s output_file=%s",
             enc_fn, cb_fn, out_fn);

    FILE* fcb = fopen(cb_fn, "r");
    if (!fcb) {
        log_error("decoder", "cannot_open_codebook file=%s", cb_fn);
        log_info("decoder", "finish status=error");
        return 1;
    }

    // 建立 Huffman 解碼樹
    DNode* root = create_node();

    char line[512];
    long total_symbols = 0;   // 之後用來決定要解出幾個字元

    while (fgets(line, sizeof(line), fcb)) {
        // 解析 symbol
        char symbol = parse_symbol(line);

        // 找到第一個欄位結束的雙引號位置
        char* p = strchr(line + 1, '"');
        if (!p) continue;
        p++;       // 指到逗號位置
        if (*p == ',') p++;

        long count = 0;
        double prob = 0.0;
        char code[256] = {0};
        double self_info = 0.0;

        // 剩下的格式: count,prob,"code",self_info
        // 用 sscanf 抓
        if (sscanf(p, "%ld,%lf,\"%255[01]\",%lf",
                   &count, &prob, code, &self_info) == 4) {

            insert_code(root, code, symbol);
            total_symbols += count;
        }
    }
    fclose(fcb);

    // 開啟 encoded.bin (rb) 與 output.txt (w)
    FILE* fenc = fopen(enc_fn, "rb");
    if (!fenc) {
        log_error("decoder", "cannot_open_encoded_file file=%s", enc_fn);
        log_info("decoder", "finish status=error");
        free_tree(root);
        return 1;
    }

    FILE* fout = fopen(out_fn, "w");
    if (!fout) {
        log_error("decoder", "cannot_open_output_file file=%s", out_fn);
        log_info("decoder", "finish status=error");
        fclose(fenc);
        free_tree(root);
        return 1;
    }

    // --------- 開始逐 bit 解碼 ---------
    DNode* cur = root;
    unsigned char byte;
    long decoded_symbols = 0;
    long bit_position = 0;    // 若發生錯誤可以記錄到第幾個 bit

    while (fread(&byte, 1, 1, fenc) == 1 && decoded_symbols < total_symbols) {
        // 從最高位元開始（bit 7 → bit 0）
        for (int b = 7; b >= 0 && decoded_symbols < total_symbols; b--) {
            int bit = (byte >> b) & 1;
            bit_position++;

            if (bit == 0) cur = cur->left;
            else          cur = cur->right;

            if (!cur) {
                // 代表 bitstream 中出現無法對應的路徑
                log_error("decoder",
                          "invalid_codeword bit_position=%ld reason=unexpected_prefix",
                          bit_position);
                log_info("decoder", "finish status=error");
                fclose(fenc);
                fclose(fout);
                free_tree(root);
                return 1;
            }

            if (cur->isLeaf) {
                fputc(cur->symbol, fout);
                decoded_symbols++;
                cur = root;   // 回到根節點，準備解下一個 symbol
            }
        }
    }

    fclose(fenc);
    fclose(fout);

    // --------- log: summary ---------
    const char* status = (decoded_symbols == total_symbols) ? "ok" : "mismatch";

    log_info("metrics",
             "summary input_encoded=%s input_codebook=%s output_file=%s "
             "num_decoded_symbols=%ld expected_symbols=%ld status=%s",
             enc_fn, cb_fn, out_fn,
             decoded_symbols, total_symbols, status);

    // --------- log: finish ---------
    if (decoded_symbols == total_symbols) {
        log_info("decoder", "finish status=ok");
    } else {
        log_info("decoder", "finish status=error");
    }

    free_tree(root);
    return (decoded_symbols == total_symbols) ? 0 : 1;
}
