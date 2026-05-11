import re
import sys

with open('cmpower原理图.svg', 'r', encoding='utf-8') as f:
    content = f.read()

# Find all node labels in foreignObject > div > span
nodes = re.findall(r'<foreignObject[^>]*>(.*?)</foreignObject>', content, re.DOTALL)
for i, fo in enumerate(nodes):
    texts = re.findall(r'<span[^>]*>([^<]*)</span>', fo)
    if texts:
        label = ' | '.join([t.strip() for t in texts if t.strip()])
        if label:
            sys.stdout.write('Node %d: %s\n' % (i, label))

# Find edge labels
edges = re.findall(r'<div class="edgeLabel"[^>]*>(.*?)</div>', content, re.DOTALL)
for i, e in enumerate(edges):
    texts = re.findall(r'<span[^>]*>([^<]*)</span>', e)
    if texts:
        label = ' '.join([t.strip() for t in texts if t.strip()])
        if label:
            sys.stdout.write('Edge %d: %s\n' % (i, label))

# Also try to find all text elements
texts = re.findall(r'<text[^>]*>([^<]+)</text>', content)
if texts:
    sys.stdout.write('\nText elements:\n')
    for t in texts:
        sys.stdout.write('  %s\n' % t.strip())
