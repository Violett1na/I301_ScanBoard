# pdf_text_extract.py —— 纯标准库 PDF 文本提取(只读, 无第三方依赖)
# 用途: 从 PDF 内容流提取可读文本, 供检索关键段落(如引脚表)。
# 局限: 不处理 CID 字体映射, 中文可能乱码; ASCII 内容(引脚名)通常可读。
# 用法: python tools/pdf_text_extract.py <file.pdf> [关键字]
#   有关键字时输出含关键字的上下文行; 无关键字时输出全文。
import re
import sys
import zlib


def extract_streams(data):
    """提取所有 stream 对象并尝试 zlib 解压"""
    out = []
    for m in re.finditer(rb'stream\r?\n', data):
        start = m.end()
        end = data.find(b'endstream', start)
        if end < 0:
            continue
        raw = data[start:end]
        try:
            out.append(zlib.decompress(raw))
        except Exception:
            continue
    return out


def extract_text(streams):
    """从内容流提取文本操作符内容"""
    texts = []
    for s in streams:
        if b'BT' not in s:
            continue
        # 提取括号字符串: (xxx)Tj 与 TJ 数组中的 (xxx)
        for m in re.finditer(rb'\((?:[^()\\]|\\.)*\)', s):
            t = m.group(0)[1:-1]
            t = t.replace(b'\\(', b'(').replace(b'\\)', b')')
            t = t.replace(b'\\\\', b'\\')
            try:
                texts.append(t.decode('latin-1'))
            except Exception:
                continue
    return texts


def main():
    if len(sys.argv) < 2:
        print('用法: python pdf_text_extract.py <file.pdf> [关键字]')
        return 2

    path = sys.argv[1]
    kw = sys.argv[2] if len(sys.argv) > 2 else None

    with open(path, 'rb') as f:
        data = f.read()

    streams = extract_streams(data)
    texts = extract_text(streams)
    full = '\n'.join(texts)

    if kw is None:
        print(full)
        return 0

    lines = full.split('\n')
    hits = [(i, ln) for i, ln in enumerate(lines) if kw.lower() in ln.lower()]
    print('命中 %d 行 (关键字: %s)' % (len(hits), kw))
    for i, ln in hits:
        lo = max(0, i - 3)
        hi = min(len(lines), i + 4)
        for j in range(lo, hi):
            mark = '>>' if j == i else '  '
            print('%s %s' % (mark, lines[j]))
        print('----')
    return 0


if __name__ == '__main__':
    sys.exit(main())
