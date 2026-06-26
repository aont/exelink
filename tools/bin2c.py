import sys
from pathlib import Path

if len(sys.argv) != 4:
    print('usage: bin2c.py INPUT SYMBOL OUTPUT', file=sys.stderr)
    sys.exit(2)

data = Path(sys.argv[1]).read_bytes()
symbol = sys.argv[2]
out = Path(sys.argv[3])
with out.open('w', newline='\n') as f:
    f.write('static const unsigned char %s[] = {\n' % symbol)
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        f.write('    ' + ', '.join('0x%02x' % b for b in chunk))
        if i + 12 < len(data):
            f.write(',')
        f.write('\n')
    f.write('};\n')
    f.write('static const unsigned int %s_len = %d;\n' % (symbol, len(data)))
