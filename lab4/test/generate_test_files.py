#!/bin/env python
import os
import shutil
import random

TMP_DIR = './_test_files'
LARGE_FILE = './large.txt'
LARGE_FILE_SIZE = 1997773
LARGE_FILE_CONTENT = ''.join([f'{i:_<9}\n' for i in range(LARGE_FILE_SIZE // 10)]) + 'end'
assert(len(LARGE_FILE_CONTENT) == LARGE_FILE_SIZE)

SMALL_DIR = './small'
SMALL_FILES = [os.path.join(SMALL_DIR, f's{i:02}.txt') for i in range(20)]
SMALL_FILES_SIZE = [(i + 1)*4 for i in range(20)]
SMALL_FILES_CONTENT = [f'{i:04}' * (i + 1) for i in range(20)]


CHARS = 'abcdefghijklmnopqrstuvwxyz0123456789'
def random_name(l, r: random.Random):
    return ''.join(r.choices(CHARS, k=l))

def generate_tree_depth(depth, prefix="", r=random.Random(0)):
    if depth == 0:
        return r.choice([None, {}])
    tree = {}
    n = r.randint(1, 4)
    for i in range(n):
        d = depth - 1 if n == 0 else r.randint(0, depth - 1)
        p = prefix + chr(ord('a') + i)
        tree[p] = generate_tree_depth(d, p, r)
    return tree

TREE_DIR = './tree'
TREE = generate_tree_depth(6)

def create_tree(t, path):
    for name, sub in t.items():
        if sub is None:
            os.mknod(os.path.join(path, name), mode=0o666)
        else:
            os.mkdir(os.path.join(path, name), mode=0o777)
            create_tree(sub, os.path.join(path, name))


if __name__ == '__main__':
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    shutil.rmtree(TMP_DIR, ignore_errors=True)
    os.makedirs(TMP_DIR, exist_ok=True)

    os.chdir(TMP_DIR)
    os.makedirs(SMALL_DIR, mode=0o777, exist_ok=True)
    for i in range(20):
        name = os.path.join(SMALL_DIR, f's{i:02}.txt')
        os.mknod(name, mode=0o666)
        with open(name, 'w') as f:
            f.write(SMALL_FILES_CONTENT[i])

    os.mknod(LARGE_FILE, mode=0o666)
    with open(LARGE_FILE, 'w') as f:
        f.write(LARGE_FILE_CONTENT)
    
    os.makedirs(TREE_DIR, mode=0o777, exist_ok=True)
    create_tree(TREE, TREE_DIR)
