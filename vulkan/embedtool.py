import functools
import sys

input_path = sys.argv[1]
output_path = sys.argv[2]
array_name = sys.argv[3]

size = 0

with open(input_path, 'rb') as input_file, open(output_path, 'w') as output_file:
    output_file.write('#include <stddef.h>\n')
    output_file.write('#include <stdint.h>\n')
    output_file.write('static const uint8_t ' + array_name + '[] = {')

    for byte in iter(functools.partial(input_file.read, 1), b''):
        if size != 0:
            output_file.write(',')
        output_file.write(str(byte[0]))
        size += 1

    output_file.write('};\n')
    output_file.write('static const size_t ' + array_name + '_len = ' + str(size) + 'ULL;\n')
