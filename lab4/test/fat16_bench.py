import os, sys
import time
import random
from time import perf_counter as pc

def bench(f, *args, **kwargs):
    t = pc()
    f(*args, **kwargs)
    return pc() - t

CHARS = 'abcdefghijklmnopqrstuvwxyz0123456789'
def random_name(l, r: random.Random):
    return ''.join(r.choices(CHARS, k=l))

def workload(dir):
    random.seed(0)
    os.chdir(dir)
    os.makedirs('bench/', mode=0o777, exist_ok=True)
    os.chdir('bench/')

    # Create N files
    N = 40
    files = []
    for i in range(N):
        level = random.randint(1, 6)
        dir = os.path.join('bench', *[random_name(2, random) for _ in range(level)])
        os.makedirs(dir, mode=0o777, exist_ok=True)
        f = os.path.join(dir, f'f{i:02}.txt')
        files.append(f)

    print(f'Creating {N} files: {files}', flush=True)
    for f in files:
        os.mknod(f, mode=0o666)
    print(f'Created {N} files.')

    # random write to random files
    T = 2000
    for i in range(T):
        f = random.choice(files)
        with open(f, 'a') as f:
            f.write('a' * 2333)
        if i % 100 == 0:
            print(f'Written {i}/{T} times.', flush=True)
    
    # random read from random files
    for i in range(T):
        f = random.choice(files)
        size = os.path.getsize(f)
        st = random.randint(0, size - 2333)
        with open(f, 'r') as f:
            f.seek(st)
            f.read(2333)
        if i % 100 == 0:
            print(f'Read {i}/{T} times.', flush=True)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Usage: python3 fat16_bench.py <dir>')
        exit(1)
    dir = sys.argv[1]
    print(f'Run benchmark in {dir}')
    print(f'{bench(workload, dir)}', flush=True)