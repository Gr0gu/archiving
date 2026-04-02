#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pipeline.h"

typedef struct Node Node;

struct Node {
  char data;
  unsigned frequency;
  Node* left;
  Node* right;
};

typedef struct {
  unsigned size;
  unsigned capacity;
  Node** array;
} MinHeap;

Node* newNode(char data, unsigned frequency) {
  Node* node = (Node*)malloc(sizeof(Node));

  node->left = NULL;
  node->right = NULL;
  node->data = data;
  node->frequency = frequency;

  return node;
}

MinHeap* createMinHeap(unsigned capacity) {
  MinHeap* minHeap = (MinHeap*)malloc(sizeof(MinHeap));

  minHeap->size = 0;
  minHeap->capacity = capacity;
  minHeap->array = (Node**)malloc(sizeof(Node*) * capacity);

  return minHeap;
}

void swapNodes(Node** a, Node** b) {
  Node* temp = *a;
  *a = *b;
  *b = temp;
}

void heapify(MinHeap* minHeap, int index) {
  int smallest = index;
  int left = index * 2 + 1;
  int right = index * 2 + 2;

  if (left < minHeap->size &&
      minHeap->array[left]->frequency < minHeap->array[smallest]->frequency)
    smallest = left;

  if (right < minHeap->size &&
      minHeap->array[right]->frequency < minHeap->array[smallest]->frequency)
    smallest = right;

  if (smallest != index) {
    swapNodes(&minHeap->array[smallest], &minHeap->array[index]);
    heapify(minHeap, smallest);
  }
}

Node* getMin(MinHeap* minHeap) {
  if (minHeap->size == 0)
    return NULL;

  Node* node = minHeap->array[0];

  minHeap->array[0] = minHeap->array[minHeap->size - 1];
  minHeap->size -= 1;
  heapify(minHeap, 0);

  return node;
}

void insertNode(MinHeap* minHeap, Node* node) {
  minHeap->size += 1;
  int i = minHeap->size - 1;
  while (i && node->frequency < minHeap->array[(i - 1) / 2]->frequency) {
    minHeap->array[i] = minHeap->array[(i - 1) / 2];
    i = (i - 1) / 2;
  }

  minHeap->array[i] = node;
}

void buildMinHeap(MinHeap* minHeap) {
  int n = minHeap->size - 1;
  for (int i = (n - 1) / 2; i >= 0; i--) {
    heapify(minHeap, i);
  }
}

int isLeaf(Node* node) {
  return !(node->left) && !(node->right);
}

MinHeap* createAndBuildMinHeap(char* data, int* frequences, int size) {
  MinHeap* minHeap = createMinHeap(size);

  for (int i = 0; i < size; i++)
    minHeap->array[i] = newNode(data[i], frequences[i]);

  minHeap->size = size;
  buildMinHeap(minHeap);

  return minHeap;
}

Node* buildHuffmanTree(char* data,
                       int* frequences,
                       int size,
                       MinHeap** outMinHeap) {
  Node *top, *left, *right;
  MinHeap* minHeap = createAndBuildMinHeap(data, frequences, size);

  while (minHeap->size != 1) {
    left = getMin(minHeap);
    right = getMin(minHeap);

    top = newNode('$', left->frequency + right->frequency);
    top->left = left;
    top->right = right;

    insertNode(minHeap, top);
  }

  *outMinHeap = minHeap;

  return getMin(minHeap);
}

void freeHuffmanTree(Node* root) {
  if (root == NULL) {
    return;
  }

  freeHuffmanTree(root->left);
  freeHuffmanTree(root->right);

  free(root);
}

void freeMinHeap(MinHeap* minHeap) {
  if (minHeap == NULL) {
    return;
  }

  free(minHeap->array);
  free(minHeap);
}

typedef struct {
  uint8_t bits[256];
  uint16_t length;
} HuffCode;

static void buildCodeTable(Node* root,
                           uint8_t* path,
                           uint16_t depth,
                           HuffCode codes[256]) {
  if (!root) {
    return;
  }

  if (isLeaf(root)) {
    uint8_t sym = (uint8_t)root->data;
    if (depth == 0) {
      // Single-symbol input edge case.
      codes[sym].bits[0] = 0;
      codes[sym].length = 1;
    } else {
      memcpy(codes[sym].bits, path, depth);
      codes[sym].length = depth;
    }
    return;
  }

  path[depth] = 0;
  buildCodeTable(root->left, path, depth + 1, codes);

  path[depth] = 1;
  buildCodeTable(root->right, path, depth + 1, codes);
}

static void write_u32(uint8_t* dst, uint32_t v) {
  memcpy(dst, &v, sizeof(v));
}

static uint32_t read_u32(const uint8_t* src) {
  uint32_t v;
  memcpy(&v, src, sizeof(v));
  return v;
}

uint8_t* apply_huffman(const uint8_t* input, size_t len, size_t* out_len) {
  if (!out_len) {
    return NULL;
  }

  *out_len = 0;

  // Format:
  // [u32 original_len]
  // [256 * u32 frequency table]
  // [u32 bit_count]
  // [bitstream bytes]
  const size_t header_size =
      sizeof(uint32_t) + (256 * sizeof(uint32_t)) + sizeof(uint32_t);

  uint32_t freq_table[256] = {0};
  for (size_t i = 0; i < len; i++) {
    freq_table[input[i]]++;
  }

  if (len == 0) {
    uint8_t* out = (uint8_t*)calloc(1, header_size);
    if (!out) {
      return NULL;
    }

    write_u32(out, 0);
    for (int i = 0; i < 256; i++) {
      write_u32(out + sizeof(uint32_t) + i * sizeof(uint32_t), 0);
    }
    write_u32(out + sizeof(uint32_t) + 256 * sizeof(uint32_t), 0);

    *out_len = header_size;
    return out;
  }

  // Build compact symbol/frequency arrays for existing heap API.
  char symbols[256];
  int frequencies[256];
  int unique = 0;
  for (int i = 0; i < 256; i++) {
    if (freq_table[i] > 0) {
      symbols[unique] = (char)i;
      frequencies[unique] = (int)freq_table[i];
      unique++;
    }
  }

  MinHeap* minHeap = NULL;
  Node* root = buildHuffmanTree(symbols, frequencies, unique, &minHeap);

  HuffCode codes[256] = {0};
  uint8_t path[256] = {0};
  buildCodeTable(root, path, 0, codes);

  size_t total_bits = 0;
  for (int i = 0; i < 256; i++) {
    if (freq_table[i] > 0) {
      total_bits += (size_t)freq_table[i] * (size_t)codes[i].length;
    }
  }

  size_t bitstream_bytes = (total_bits + 7) / 8;
  size_t total_size = header_size + bitstream_bytes;

  uint8_t* out = (uint8_t*)calloc(1, total_size);
  if (!out) {
    freeHuffmanTree(root);
    freeMinHeap(minHeap);
    return NULL;
  }

  // Write header
  write_u32(out, (uint32_t)len);
  for (int i = 0; i < 256; i++) {
    write_u32(out + sizeof(uint32_t) + i * sizeof(uint32_t), freq_table[i]);
  }
  write_u32(out + sizeof(uint32_t) + 256 * sizeof(uint32_t),
            (uint32_t)total_bits);

  // Write bitstream
  uint8_t* bitstream = out + header_size;
  size_t bit_pos = 0;
  for (size_t i = 0; i < len; i++) {
    const HuffCode* c = &codes[input[i]];
    for (uint16_t b = 0; b < c->length; b++) {
      if (c->bits[b]) {
        bitstream[bit_pos / 8] |= (uint8_t)(1u << (7 - (bit_pos % 8)));
      }
      bit_pos++;
    }
  }

  *out_len = total_size;

  freeHuffmanTree(root);
  freeMinHeap(minHeap);
  return out;
}

uint8_t* inverse_huffman(const uint8_t* input, size_t len, size_t* out_len) {
  if (!input || !out_len) {
    return NULL;
  }

  *out_len = 0;

  const size_t header_size =
      sizeof(uint32_t) + (256 * sizeof(uint32_t)) + sizeof(uint32_t);
  if (len < header_size) {
    return NULL;
  }

  uint32_t original_len = read_u32(input);

  uint32_t freq_table[256];
  for (int i = 0; i < 256; i++) {
    freq_table[i] = read_u32(input + sizeof(uint32_t) + i * sizeof(uint32_t));
  }

  uint32_t total_bits =
      read_u32(input + sizeof(uint32_t) + 256 * sizeof(uint32_t));

  const uint8_t* bitstream = input + header_size;
  size_t bitstream_bytes = (size_t)(len - header_size);
  if (((size_t)total_bits + 7) / 8 > bitstream_bytes) {
    return NULL;
  }

  uint8_t* out = NULL;
  if (original_len == 0) {
    out = (uint8_t*)malloc(1);
    if (!out)
      return NULL;
    *out_len = 0;
    return out;
  }

  out = (uint8_t*)malloc(original_len);
  if (!out) {
    return NULL;
  }

  char symbols[256];
  int frequencies[256];
  int unique = 0;
  for (int i = 0; i < 256; i++) {
    if (freq_table[i] > 0) {
      symbols[unique] = (char)i;
      frequencies[unique] = (int)freq_table[i];
      unique++;
    }
  }

  if (unique == 0) {
    free(out);
    return NULL;
  }

  MinHeap* minHeap = NULL;
  Node* root = buildHuffmanTree(symbols, frequencies, unique, &minHeap);

  // Single-symbol edge case: no traversal needed.
  if (isLeaf(root)) {
    memset(out, (uint8_t)root->data, original_len);
    *out_len = original_len;
    freeHuffmanTree(root);
    freeMinHeap(minHeap);
    return out;
  }

  size_t out_pos = 0;
  Node* cur = root;

  for (uint32_t bit_pos = 0; bit_pos < total_bits && out_pos < original_len;
       bit_pos++) {
    uint8_t byte = bitstream[bit_pos / 8];
    uint8_t bit = (byte >> (7 - (bit_pos % 8))) & 1u;

    cur = bit ? cur->right : cur->left;
    if (!cur) {
      free(out);
      freeHuffmanTree(root);
      freeMinHeap(minHeap);
      return NULL;
    }

    if (isLeaf(cur)) {
      out[out_pos++] = (uint8_t)cur->data;
      cur = root;
    }
  }

  if (out_pos != original_len) {
    free(out);
    freeHuffmanTree(root);
    freeMinHeap(minHeap);
    return NULL;
  }

  *out_len = out_pos;

  freeHuffmanTree(root);
  freeMinHeap(minHeap);
  return out;
}