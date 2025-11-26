#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "logger.h"

// -----------------------------
// Huffman Tree Node
// -----------------------------
typedef struct Node {
    char symbol;
    long count;
    double probability;
    struct Node *left, *right;
    char code[256];
} Node;

// Min-heap 用來建 Huffman Tree
Node* heap[300];
int heapSize = 0;

void push(Node* n) {
    heap[++heapSize] = n;
    int i = heapSize;
    while (i > 1 && heap[i]->count < heap[i/2]->count) {
        Node* tmp = heap[i];
        heap[i] = heap[i/2];
        heap[i/2] = tmp;
        i /= 2;
    }
}

Node* pop() {
    Node* root = heap[1];
    heap[1] = heap[heapSize--];

    int i = 1;
    while (1) {
        int left = i * 2;
        int right = i * 2 + 1;
        int smallest = i;

        if (left <= heapSize && heap[left]->count < heap[smallest]->count)
            smallest = left;
        if (right <= heapSize && heap[right]->count < heap[smallest]->count)
            smallest = right;

        if (smallest == i) break;

        Node* tmp = heap[i];
        heap[i] = heap[smallest];
        heap[smallest] = tmp;
        i = smallest;
    }
    return root;
}

// -----------------------------
// 遞迴產生 codeword
// -----------------------------
void generateCodes(Node* root, char* prefix) {
    if (!root) return;
    if (!root->left && !root->right) {
        strcpy(root->code, prefix);
        return;
    }
    char leftPrefix[256], rightPrefix[256];

    sprintf(leftPrefix, "%s0", prefix);
    sprintf(rightPrefix, "%s1", prefix);

    generateCodes(root->left, leftPrefix);
    generateCodes(root->right, rightPrefix);
}

// -----------------------------
// 依排序輸出 codebook 用
// -----------------------------
int compareNode(const void* a, const void* b) {
    Node* na = *(Node**)a;
    Node* nb = *(Node**)b;

    if (na->count != nb->count)
        return (na->count - nb->count);
    return (na->symbol - nb->symbol);
}

// -----------------------------
// 主程式
// -----------------------------
int main(int argc, char* argv[]) {

    if (argc != 4) {
        printf("Usage: ./encoder.exe input.txt codebook.csv encoded.bin\n");
        return 1;
    }

    char* input_file = argv[1];
    char* codebook_file = argv[2];
    char* encoded_file = argv[3];

    // log 開始
    log_info("encoder", "start input_file=%s", input_file);

    FILE* fin = fopen(input_file, "r");
    if (!fin) {
        log_error("encoder", "cannot_open_input_file file=%s", input_file);
        return 1;
    }

    long freq[256] = {0};
    long total_symbols = 0;
    int c;

    // 統計字元
    while ((c = fgetc(fin)) != EOF) {
        freq[c]++;
        total_symbols++;
    }
    fclose(fin);

    // 建 heap
    int nodeCount = 0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            Node* n = (Node*)malloc(sizeof(Node));
            n->symbol = (char)i;
            n->count = freq[i];
            n->probability = 0.0;
            n->left = n->right = NULL;
            n->code[0] = '\0';
            push(n);
            nodeCount++;
        }
    }

    // 建 Huffman Tree
    while (heapSize > 1) {
        Node* a = pop();
        Node* b = pop();

        Node* p = (Node*)malloc(sizeof(Node));
        p->symbol = '\0';
        p->count = a->count + b->count;
        p->left = a;
        p->right = b;
        p->code[0] = '\0';
        push(p);
    }

    Node* root = pop();

    // 生成編碼
    generateCodes(root, "");

    // 建 node list for sorting
    Node* list[300];
    int idx = 0;

    void collect(Node* n) {
        if (!n) return;
        if (!n->left && !n->right) {
            list[idx++] = n;
            return;
        }
        collect(n->left);
        collect(n->right);
    }
    collect(root);

    // compute probability
    for (int i = 0; i < idx; i++) {
        list[i]->probability = (double)list[i]->count / total_symbols;
    }

    // 排序 codebook（count → symbol）
    qsort(list, idx, sizeof(Node*), compareNode);

    // 輸出 codebook.csv
    FILE* fcb = fopen(codebook_file, "w");
    for (int i = 0; i < idx; i++) {
        double p = list[i]->probability;
        double self_info = log2(1.0 / p);
        fprintf(fcb, "\"%c\",%ld,%.15f,\"%s\",%.15f\n",
                list[i]->symbol,
                list[i]->count,
                p,
                list[i]->code,
                self_info);
    }
    fclose(fcb);

    // -----------------------------
    // encode input → bit stream
    // -----------------------------
    fin = fopen(input_file, "r");
    FILE* fout = fopen(encoded_file, "wb");

    unsigned char byte = 0;
    int bitCount = 0;

    long total_huffman_bits = 0;

    // 用來查字典
    char* dict[256] = {0};
    for (int i = 0; i < idx; i++) {
        dict[(unsigned char)list[i]->symbol] = list[i]->code;
    }

    while ((c = fgetc(fin)) != EOF) {
        char* code = dict[c];
        for (int k = 0; code[k] != '\0'; k++) {

            byte = (byte << 1) | (code[k] - '0');
            bitCount++;
            total_huffman_bits++;

            if (bitCount == 8) {
                fwrite(&byte, 1, 1, fout);
                byte = 0;
                bitCount = 0;
            }
        }
    }
    fclose(fin);

    // padding bits (zero)
    if (bitCount > 0) {
        byte <<= (8 - bitCount);
        fwrite(&byte, 1, 1, fout);
    }
    fclose(fout);

    // -----------------------------
    // 計算 entropy / perplexity
    // -----------------------------
    double entropy = 0.0;
    double avg_len = 0.0;

    for (int i = 0; i < idx; i++) {
        double p = list[i]->probability;
        entropy += p * log2(1.0 / p);
        avg_len += p * strlen(list[i]->code);
    }

    double perplexity = pow(2.0, entropy);

    long total_bits_fixed = total_symbols * 8;

    double compression_ratio = (double)total_bits_fixed / total_huffman_bits;
    double saving_percentage = 1.0 - ((double)total_huffman_bits / total_bits_fixed);

    // -----------------------------
    // log summary
    // -----------------------------
    log_info("metrics",
        "summary input_file=%s output_codebook=%s output_encoded=%s "
        "num_symbols=%ld fixed_code_bits_per_symbol=8 "
        "entropy_bits_per_symbol=%.3f perplexity=%.3f "
        "huffman_bits_per_symbol=%.3f total_bits_fixed=%ld total_bits_huffman=%ld "
        "compression_ratio=%.9f saving_percentage=%.3f",
        input_file, codebook_file, encoded_file,
        total_symbols,
        entropy,
        perplexity,
        avg_len,
        total_bits_fixed,
        total_huffman_bits,
        compression_ratio,
        saving_percentage
    );

    // 結束
    log_info("encoder", "finish status=ok");

    return 0;
}

