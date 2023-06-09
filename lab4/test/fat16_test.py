import os
import random
import unittest

from generate_test_files import *

FAT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fat16')


class TestFat16List(unittest.TestCase):
    def check_tree(self, t, path):
        for name, sub in t.items():
            if sub is None:
                file = os.path.join(path, name)
                self.assertTrue(os.path.exists(file), f'{file} does not exist')
                self.assertTrue(os.path.isfile(file), f'{file} is not a file')
            else:
                dir = os.path.join(path, name)
                self.assertTrue(os.path.exists(dir), f'{dir} does not exist')
                self.assertTrue(os.path.isdir(dir), f'{dir} is not a directory')
                self.check_tree(sub, dir)

    def test1_list_tree(self):
        os.chdir(FAT_DIR)
        self.check_tree(TREE, TREE_DIR)


class TestFat16Read(unittest.TestCase):
    def test1_file_exists(self):
        os.chdir(FAT_DIR)
        self.assertTrue(os.path.exists(FAT_DIR), f'{FAT_DIR} does not exist')
        self.assertTrue(os.path.isdir(FAT_DIR), f'{FAT_DIR} is not a directory')
        self.assertTrue(os.path.exists(LARGE_FILE), f'{LARGE_FILE} does not exist')
        self.assertTrue(os.path.isfile(LARGE_FILE), f'{LARGE_FILE} is not a file')
        self.assertTrue(os.path.exists(SMALL_DIR), f'{SMALL_DIR} does not exist')
        self.assertTrue(os.path.isdir(SMALL_DIR), f'{SMALL_DIR} is not a directory')
        for f in SMALL_FILES:
            self.assertTrue(os.path.exists(f), f'{f} does not exist')
            self.assertTrue(os.path.isfile(f), f'{f} is not a file')

    def test2_seq_read_large_file(self):
        os.chdir(FAT_DIR)
        self.assertTrue(os.path.getsize(LARGE_FILE) == LARGE_FILE_SIZE,
                    f'{LARGE_FILE} is not {LARGE_FILE_SIZE} bytes')
        with open(LARGE_FILE, 'r') as f:
            content = f.read()
        for i in range(LARGE_FILE_SIZE // 10):
            c = content[i*10: (i+1)*10]
            expected = LARGE_FILE_CONTENT[i*10: (i+1)*10]
            self.assertEqual(c, expected,
                        f'{LARGE_FILE} does not match expected content at line {i}')

    def test3_rand_read_large_file(self):
        os.chdir(FAT_DIR)
        N = 1000
        random.seed(0)
        with open(LARGE_FILE, 'r') as f:
            for i in range(N):
                len = random.randint(1, 32768)
                start = random.randint(0, LARGE_FILE_SIZE - len)
                f.seek(start)
                content = f.read(len)
                expected = LARGE_FILE_CONTENT[start: start + len]
                self.assertEqual(content, expected,
                            f'{LARGE_FILE} does not match expected content, start={start}, len={len}')

    def test4_seq_read_small_files(self):
        os.chdir(FAT_DIR)
        for f, content in zip(SMALL_FILES, SMALL_FILES_CONTENT):
            with open(f, 'r') as f:
                self.assertEqual(f.read(), content, f'{f} does not match expected content')


class TestFat16CreateRemove(unittest.TestCase):
    def check_tree(self, t, path):
        for name, sub in t.items():
            if sub is None:
                file = os.path.join(path, name)
                self.assertTrue(os.path.exists(file), f'{file} does not exist')
                self.assertTrue(os.path.isfile(file), f'{file} is not a file')
            else:
                dir = os.path.join(path, name)
                self.assertTrue(os.path.exists(dir), f'{dir} does not exist')
                self.assertTrue(os.path.isdir(dir), f'{dir} is not a directory')
                self.check_tree(sub, dir)

    def test1_file_create_root(self):
        os.chdir(FAT_DIR)
        name = os.path.join(FAT_DIR, 'newfile.txt')
        os.mknod(name, mode=0o666)
        self.assertTrue(os.path.exists(name), f'create file {name}, but it does not exist')
        self.assertTrue(os.path.isfile(name), f'{name} is not a file')

    def test2_file_remove_root(self):
        os.chdir(FAT_DIR)
        for i in range(20):
            name = os.path.join(FAT_DIR, f'fordel{i:02}.txt')
            os.mknod(name, mode=0o666)
            self.assertTrue(os.path.exists(name), f'create file {name}, but it does not exist')
            self.assertTrue(os.path.isfile(name), f'{name} is not a file')

        for i in range(20):
            name = os.path.join(FAT_DIR, f'fordel{i:02}.txt')
            os.remove(name)
            self.assertFalse(os.path.exists(name), f'remove {name}, but it still exists')

    def test3_dir_create_root(self):
        os.chdir(FAT_DIR)
        name = os.path.join(FAT_DIR, 'newdir')
        os.mkdir(name, mode=0o777)
        self.assertTrue(os.path.exists(name), f'create {name}, but it does not exist')
        self.assertTrue(os.path.isdir(name), f'{name} is not a directory')

    def test4_dir_remove_root(self):
        os.chdir(FAT_DIR)
        for i in range(20):
            name = os.path.join(FAT_DIR, f'fordel{i:02}')
            os.mkdir(name, mode=0o777)
            self.assertTrue(os.path.exists(name), f'create dir {name}, but it does not exist')
            self.assertTrue(os.path.isdir(name), f'{name} is not a directory')

        for i in range(20):
            name = os.path.join(FAT_DIR, f'fordel{i:02}')
            os.rmdir(name)
            self.assertFalse(os.path.exists(name), f'remove dir {name}, but it still exists')

    def test5_file_create_subdir(self):
        os.chdir(FAT_DIR)
        dir = os.path.join(FAT_DIR, 'subdir')
        os.mkdir(dir, mode=0o777)
        self.assertTrue(os.path.exists(dir), f'create {dir}, but it does not exist')
        self.assertTrue(os.path.isdir(dir), f'{dir} is not a directory')

        subdir = os.path.join(dir, 'subsubdir')
        os.mkdir(subdir, mode=0o777)
        self.assertTrue(os.path.exists(subdir), f'create {subdir}, but it does not exist')
        self.assertTrue(os.path.isdir(subdir), f'{subdir} is not a directory')

        name = os.path.join(subdir, 'newfile.txt')
        os.mknod(name, mode=0o666)
        self.assertTrue(os.path.exists(name), f'create file {name}, but it does not exist')
        self.assertTrue(os.path.isfile(name), f'{name} is not a file')

    def test6_file_remove_subdir(self):
        os.chdir(FAT_DIR)
        dir = os.path.join(FAT_DIR, 'subdir2')
        os.mkdir(dir, mode=0o777)
        self.assertTrue(os.path.exists(dir), f'create {dir}, but it does not exist')
        self.assertTrue(os.path.isdir(dir), f'{dir} is not a directory')

        subdir = os.path.join(dir, 'subsubdir')
        os.mkdir(subdir, mode=0o777)
        self.assertTrue(os.path.exists(subdir), f'create {subdir}, but it does not exist')
        self.assertTrue(os.path.isdir(subdir), f'{subdir} is not a directory')

        name = os.path.join(subdir, 'newfile.txt')
        os.mknod(name, mode=0o666)
        self.assertTrue(os.path.exists(name), f'create file {name}, but it does not exist')
        self.assertTrue(os.path.isfile(name), f'{name} is not a file')

        os.remove(name)
        self.assertFalse(os.path.exists(name), f'remove {name}, but it still exists')

    def test7_create_tree(self):
        os.chdir(FAT_DIR)
        tree_dir = 'tree2'
        os.mkdir(tree_dir, mode=0o777)
        create_tree(TREE, tree_dir)
        self.check_tree(TREE, tree_dir)

    def test8_remove_tree(self):
        os.chdir(FAT_DIR)
        tree_dir = 'tree3'
        os.mkdir(tree_dir, mode=0o777)
        create_tree(TREE, tree_dir)
        self.check_tree(TREE, tree_dir)
        shutil.rmtree(tree_dir, ignore_errors=True)
        self.assertFalse(os.path.exists(tree_dir), f'remove {tree_dir}, but it still exists')