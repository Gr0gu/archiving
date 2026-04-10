/*
 * huffman.c — Huffman entropy coding (encode + decode).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pipeline.h"

/* ------------------------------------------------------------------ */
/* Min-heap of Huffman nodes                                           */
/* ------------------------------------------------------------------ */

typedef struct Node Node;

struct Node {
  uint8_t  data;      /* byte symbol (leaf) or 0 (internal node) */
  uint32_t frequency;
  Node*    left;
  Node*    right;
};

typedef struct {
  unsigned  size;
  unsigned  capacity;
  Node**    array;
} MinHeap;

static Node* newNode(uint8_t data, uint32_t frequency) {
  Node* node = malloc(sizeof(Node));
  if (!node) return NULL;
  node->data      = data;
  node->frequency = frequency;
  node->left      = NULL;
  node->right     = NULL;
  return node;
}

static MinHeap* createMinHeap(unsigned capacity) {
  MinHeap* h = malloc(sizeof(MinHeap));
  if (!h) return NULL;
  h->size     = 0;
  h->capacity = capacity;
  h->array    = malloc(sizeof(Node*) * capacity);
  if (!h->array) { free(h); return NULL; }
  return h;
}

static void swapNodes(Node** a, Node** b) {
  Node* tmp = *a; *a = *b; *b = tmp;
}

static void heapify(MinHeap* h, int idx) {
  int smallest = idx;
  int left     = idx * 2 + 1;
  int right    = idx * 2 + 2;

  if (left  < (int)h->size && h->array[left]->frequency  < h->array[smallest]->frequency)
    smallest = left;
  if (right < (int)h->size && h->array[right]->frequency < h->array[smallest]->frequency)
    smallest = right;

  if (smallest != idx) {
    swapNodes(&h->array[smallest], &h->array[idx]);
    heapify(h, smallest);
  }
}

static Node* getMin(MinHeap* h) {
  if (h->size == 0) return NULL;
  Node* node = h->array[0];
  h->array[0] = h->array[--h->size];
  heapify(h, 0);
  return node;
}

static void insertNode(MinHeap* h, Node* node) {
  /* Guard: the heap must never exceed its allocated capacity. */
  if (h->size >= h->capacity) return;

  unsigned i = h->size++;
  while (i > 0 && node->frequency < h->array[(i - 1) / 2]->frequency) {
    h->array[i] = h->array[(i - 1) / 2];
    i = (i - 1) / 2;
  }
  h->array[i] = node;
}

static void buildMinHeap(MinHeap* h) {
  for (int i = ((int)h->size - 2) / 2; i >= 0; --i)
    heapify(h, i);
}

static int isLeaf(const Node* node) {
  return !node->left && !node->right;
}

/* ------------------------------------------------------------------ */
/* Tree construction                                                   */
/* ------------------------------------------------------------------ */

/*
 * Build a Huffman tree from parallel arrays of byte symbols and their
 * frequencies.  Returns the root node; also returns the MinHeap via
 * *outMinHeap so the caller can free it separately.
 *
 * The heap is sized at  2*size - 1  to accommodate all internal nodes
 * that are inserted during the merge phase (size-1 of them).
 */
static Node* buildHuffmanTree(const uint8_t*  symbols,
                               const uint32_t* frequencies,
                               int             size,
                               MinHeap**       outMinHeap) {
  /* Capacity = 2*size-1: size leaves + (size-1) internal nodes. */
  MinHeap* h = createMinHeap((unsigned)(2 * size));
  if (!h) { *outMinHeap = NULL; return NULL; }

  for (int i = 0; i < size; i++)
    h->array[h->size++] = newNode(symbols[i], frequencies[i]);

  buildMinHeap(h);

  while (h->size > 1) {
    Node* left  = getMin(h);
    Node* right = getMin(h);

    Node* top = newNode(0, left->frequency + right->frequency);
    if (!top) { *outMinHeap = h; return NULL; } /* OOM — caller must free */
    top->left  = left;
    top->right = right;

    insertNode(h, top);
  }

  *outMinHeap = h;
  return getMin(h);
}

static void freeHuffmanTree(Node* root) {
  if (!root) return;
  freeHuffmanTree(root->left);
  freeHuffmanTree(root->right);
  free(root);
}

static void freeMinHeap(MinHeap* h) {
  if (!h) return;
  free(h->array);
  free(h);
}

/* ------------------------------------------------------------------ */
/* Code table                                                          */
/* ------------------------------------------------------------------ */

/*
 * One bit per array slot — simple and readable; the depth of a Huffman
 * tree over 256 symbols is at most 255.
 */
typedef struct {
  uint8_t  bits[256];
  uint16_t length;
} HuffCode;

static void buildCodeTable(const Node* root,
                            uint8_t*    path,
                            uint16_t    depth,
                            HuffCode    codes[256]) {
  if (!root) return;

  if (isLeaf(root)) {
    uint8_t sym = root->data;
    if (depth == 0) {
      /* Only one distinct symbol in the input. */
      codes[sym].bits[0] = 0;
      codes[sym].length  = 1;
    } else {
      memcpy(codes[sym].bits, path, depth);
      codes[sym].length = depth;
    }
    return;
  }

  path[depth] = 0;
  buildCodeTable(root->left,  path, depth + 1, codes);
  path[depth] = 1;
  buildCodeTable(root->right, path, depth + 1, codes);
}

/* ------------------------------------------------------------------ */
/* I/O helpers                                                         */
/* ------------------------------------------------------------------ */

static void write_u32(uint8_t* dst, uint32_t v) { memcpy(dst, &v, 4); }
static uint32_t read_u32(const uint8_t* src) {
  uint32_t v; memcpy(&v, src, 4); return v;
}

/* Offset of frequency entry i inside the header. */
#define FREQ_OFFSET(i) (sizeof(uint32_t) + (size_t)(i) * sizeof(uint32_t))
/* Offset of the bit-count field. */
#define BITCOUNT_OFFSET (sizeof(uint32_t) + 256 * sizeof(uint32_t))
/* Total header size. */
#define HEADER_SIZE     (BITCOUNT_OFFSET + sizeof(uint32_t))

/* ------------------------------------------------------------------ */
/* apply_huffman                                                        */
/* ------------------------------------------------------------------ */

uint8_t* apply_huffman(const uint8_t* input, size_t len, size_t* out_len) {
  if (!out_len)
    return NULL;
  *out_len = 0;

  /* Build frequency table over all 256 byte values. */
  uint32_t freq_table[256] = {0};
  for (size_t i = 0; i < len; i++)
    freq_table[input[i]]++;

  /* Handle empty input: emit a valid but empty header. */
  if (len == 0) {
    uint8_t* out = calloc(1, HEADER_SIZE);
    if (!out) return NULL;
    /* All fields zero: original_len=0, all freqs=0, bit_count=0. */
    *out_len = HEADER_SIZE;
    return out;
  }

  /* Compact symbol/frequency arrays for tree construction. */
  uint8_t  symbols[256];
  uint32_t frequencies[256];
  int unique = 0;
  for (int i = 0; i < 256; i++) {
    if (freq_table[i] > 0) {
      symbols[unique]     = (uint8_t)i;
      frequencies[unique] = freq_table[i];
      unique++;
    }
  }

  MinHeap* heap = NULL;
  Node*    root = buildHuffmanTree(symbols, frequencies, unique, &heap);
  if (!root) { freeMinHeap(heap); return NULL; }

  HuffCode codes[256] = {{{0}, 0}};
  uint8_t  path[256]  = {0};
  buildCodeTable(root, path, 0, codes);

  /* Count total bits needed for the bitstream. */
  size_t total_bits = 0;
  for (int i = 0; i < 256; i++) {
    if (freq_table[i])
      total_bits += (size_t)freq_table[i] * codes[i].length;
  }

  size_t bitstream_bytes = (total_bits + 7) / 8;
  size_t total_size      = HEADER_SIZE + bitstream_bytes;

  uint8_t* out = calloc(1, total_size);
  if (!out) { freeHuffmanTree(root); freeMinHeap(heap); return NULL; }

  /* --- Write header --- */
  write_u32(out, (uint32_t)len);
  for (int i = 0; i < 256; i++)
    write_u32(out + FREQ_OFFSET(i), freq_table[i]);
  write_u32(out + BITCOUNT_OFFSET, (uint32_t)total_bits);

  /* --- Write bitstream (MSB-first) --- */
  uint8_t* bs = out + HEADER_SIZE;
  size_t   bp = 0;
  for (size_t i = 0; i < len; i++) {
    const HuffCode* c = &codes[input[i]];
    for (uint16_t b = 0; b < c->length; b++) {
      if (c->bits[b])
        bs[bp / 8] |= (uint8_t)(1u << (7 - bp % 8));
      bp++;
    }
  }

  *out_len = total_size;
  freeHuffmanTree(root);
  freeMinHeap(heap);
  return out;
}

/* ------------------------------------------------------------------ */
/* inverse_huffman                                                      */
/* ------------------------------------------------------------------ */

uint8_t* inverse_huffman(const uint8_t* input, size_t len, size_t* out_len) {
  if (!input || !out_len)
    return NULL;
  *out_len = 0;

  if (len < HEADER_SIZE)
    return NULL;

  uint32_t original_len = read_u32(input);

  uint32_t freq_table[256];
  for (int i = 0; i < 256; i++)
    freq_table[i] = read_u32(input + FREQ_OFFSET(i));

  uint32_t total_bits = read_u32(input + BITCOUNT_OFFSET);

  const uint8_t* bs = input + HEADER_SIZE;
  if (((size_t)total_bits + 7) / 8 > len - HEADER_SIZE)
    return NULL;

  /* Empty original — return a valid zero-length buffer. */
  if (original_len == 0) {
    uint8_t* out = malloc(1);
    if (!out) return NULL;
    *out_len = 0;
    return out;
  }

  uint8_t  symbols[256];
  uint32_t frequencies[256];
  int unique = 0;
  for (int i = 0; i < 256; i++) {
    if (freq_table[i]) {
      symbols[unique]     = (uint8_t)i;
      frequencies[unique] = freq_table[i];
      unique++;
    }
  }

  if (unique == 0)
    return NULL;

  MinHeap* heap = NULL;
  Node*    root = buildHuffmanTree(symbols, frequencies, unique, &heap);
  if (!root) { freeMinHeap(heap); return NULL; }

  uint8_t* out = malloc(original_len);
  if (!out) { freeHuffmanTree(root); freeMinHeap(heap); return NULL; }

  /* Single-symbol edge case: every code is the same bit. */
  if (isLeaf(root)) {
    memset(out, root->data, original_len);
    *out_len = original_len;
    freeHuffmanTree(root);
    freeMinHeap(heap);
    return out;
  }

  size_t out_pos = 0;
  Node*  cur     = root;

  for (uint32_t bp = 0; bp < total_bits && out_pos < original_len; bp++) {
    uint8_t bit = (bs[bp / 8] >> (7 - bp % 8)) & 1u;
    cur = bit ? cur->right : cur->left;

    if (!cur) {
      /* Corrupt bitstream. */
      free(out);
      freeHuffmanTree(root);
      freeMinHeap(heap);
      return NULL;
    }

    if (isLeaf(cur)) {
      out[out_pos++] = cur->data;
      cur = root;
    }
  }

  if (out_pos != original_len) {
    free(out);
    freeHuffmanTree(root);
    freeMinHeap(heap);
    return NULL;
  }

  *out_len = out_pos;
  freeHuffmanTree(root);
  freeMinHeap(heap);
  return out;
}
