#include <cslice.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct Node {
    bool leaf;
    uint32_t order;
    Slice *data, *children;
    struct Node *next;
} Node;

typedef struct Tree {
    uint32_t order;
    Node *root, *first;
} Tree;

typedef struct Loc {
    Node *node;
    KeyIndex *kx;
} Loc;

typedef struct SplitTuple {
    Node *first, *second;
    void *promoted_key;
} SplitTuple;

Node *node(uint32_t min_order) {
    Node *n = malloc(sizeof(Node));
    n->leaf = true;
    n->order = min_order;
    n->data = slice(2 * min_order - 1);
    n->children = slice(2 * min_order);
    n->next = NULL;
    return n;
}

Tree *tree(uint32_t min_order) {
    Tree *t = malloc(sizeof(Tree));
    t->root = t->first = NULL;
    t->order = min_order;
    return t;
}

Loc *loc(Node *n, KeyIndex *kx) {
    Loc *l = malloc(sizeof(Loc));
    l->node = n;
    l->kx = kx;
    return l;
}

Node *make_node(Slice *data, Slice *children, uint32_t min_order) {
    Node *n = node(min_order);
    n->data = data;
    n->children = children;
    n->leaf = true;
    return n;
}

SplitTuple *split_tuple(Node *first, Node *second, void *promoted_key) {
    SplitTuple *st = malloc(sizeof(SplitTuple));
    st->first = first;
    st->second = second;
    st->promoted_key = promoted_key;
    return st;
}

Node *root(Tree *t) {
    return t->root;
}

bool full(Node *n) {
    return len(n->data) == 2 * n->order - 1;
}

static SplitTuple *split(Node *n) {
    // split the node to be split's data slice into two subslices, of which the left one will always have a greater
    // size. then, construct two nodes out of the halves. for now, just assign an empty slice to the `children`
    // parameter of each node
    Slice *left_data = sslice(n->data, 0, len(n->data) / 2 + 1);
    Slice *right_data = sslice(n->data, len(n->data) / 2 + 1, len(n->data));
    Node *left = make_node(left_data, slice(n->order), n->order);
    Node *right = make_node(right_data, slice(n->order), n->order);
    // if the node to be split is not a leaf node, then ∃ at least len(n->data) + 1 children in the node's children
    // slice, so we need to assign those children
    if (!n->leaf) {
        // split the node's children into two children, with the left always being larger
        Slice *left_children = sslice(n->children, 0, len(n->children) / 2);
        Slice *right_children = sslice(n->children, len(n->children) / 2, len(n->children));
        // assign
        left->children = left_children;
        right->children = right_children;
        // if the node passed in wasn't a leaf then the split halves won't be either
        left->leaf = false;
        right->leaf = false;
    }
    // get (but don't pop) the last element of the left data slice and set it as the promoted key
    void *promoted_key = (void *) last(left_data);
    SplitTuple *st = split_tuple(left, right, promoted_key);
    return st;
}

static void insert_non_full(Node *n, void *key, int(*cmpfunc)(const void *, const void *)) {
    // find the index where `key` needs to be inserted
    KeyIndex *kx = find_index(n->data, key, cmpfunc);
    // if we successfully find a key that matches the `key` passed in, then we reject it because trees should not allow
    // duplicates
    if (kx_key(kx)) {
        printf("duplicate keys not allowed!\n");
        return;
    }
    // if the node is a leaf, just straight insert it into the node
    if (n->leaf) put_index(n->data, key, kx_index(kx));
    else {
        // if the node isn't a leaf, we need to find the subtree where `key` could possibly be
        Node *target = (Node *) get_index(n->children, kx_index(kx));
        // if it's full, rip it in half and reset the local child pointers that are affected
        if (full(target)) {
            SplitTuple *st = split(target);
            put_index(n->data, st->promoted_key, kx_index(kx));
            set_index(n->children, st->first, kx_index(kx));
            put_index(n->children, st->second, kx_index(kx) + 1);
            // recurse with node initially passed in
            return insert_non_full(n, key, cmpfunc);
        }
        // recurse with target
        insert_non_full(target, key, cmpfunc);
    }
}

void insert(Tree *tree, const void *key, int(*cmpfunc)(const void *, const void *)) {
    // current root
    Node *root = tree->root;
    if (full(root)) {
        // if the root is full, we need to split it and increase the height of the B+Tree
        Node *new_root = node(root->order);
        // reassign the tree's root to the new root just created
        tree->root = new_root;
        // if we are here, it is impossible for the new root to be a leaf since ∃ a node root such that the root is
        // full and is being split, so assign the new root's leaf property to false
        new_root->leaf = false;
        // split the root
        SplitTuple *st = split(root);
        // push the promoted key to the new root's data slice. then, push the (Node*) halves of the split root into the
        // new root's children slice
        push(new_root->data, st->promoted_key);
        push(new_root->children, st->first);
        push(new_root->children, st->second);
        // to maintain the B+Tree property that the bottom nodes form a linked list for easy traversal, check if the
        // prior root was a leaf, then assign the `next` pointers accordingly
        if (root->leaf) st->first->next = st->second;
        st->second->next = NULL;
        // delete the old root and proceed with insertion in a non-full node
        root = NULL;
        free(root);
        return insert_non_full(new_root, key, cmpfunc);
    }
    insert_non_full(root, key, cmpfunc);
}

Tree *make_tree(void *keys, uint32_t num_keys, uint32_t min_order, size_t key_size,
                int(*cmpfunc)(const void *, const void *)) {
    Tree *tree = tree(min_order);
    for (int i = 0; i < num_keys; i++) insert(tree, keys + i * key_size, cmpfunc);
    return tree;
}