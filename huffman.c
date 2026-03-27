#include <stdio.h>
#include <stdlib.h>

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

void printArray(int* array, int n) {
  for (int i = 0; i < n; i++)
    printf("%d ", array[i]);

  printf("\n");
}

void printCodes(Node* root, int* array, int top) {
  if (root->left) {
    array[top] = 0;
    printCodes(root->left, array, top + 1);
  }

  if (root->right) {
    array[top] = 1;
    printCodes(root->right, array, top + 1);
  }

  if (isLeaf(root)) {
    printf("%c: ", root->data);
    printArray(array, top);
  }
}

void HuffmanCodes(char* data, int* frequences, int size) {
  MinHeap* minHeap = NULL;

  Node* root = buildHuffmanTree(data, frequences, size, &minHeap);

  int* array = (int*)malloc(size * sizeof(int));
  int top = 0;
  printCodes(root, array, top);

  free(array);
  freeHuffmanTree(root);
  freeMinHeap(minHeap);
}

int main() {
  char arr[] = {'a', 'b', 'c', 'd', 'e', 'f'};
  int freq[] = {5, 9, 12, 13, 16, 45};

  int size = sizeof(arr) / sizeof(arr[0]);

  HuffmanCodes(arr, freq, size);

  return 0;
}