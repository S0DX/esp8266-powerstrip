import re

with open('cmpower原理图.svg', 'r', encoding='utf-8') as f:
    content = f.read()

# Extract all text content between tags
content_between = re.findall(r'>([^<]+)<', content)
for c in content_between:
    c = c.strip()
    if c and len(c) > 1:
        print(repr(c))
