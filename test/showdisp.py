import sys
from PIL import Image
import numpy as np
import binascii
from pprint import pprint

def main():
	# put the hex dump in the first arg
	hex_data = sys.argv[1]
	if(len(sys.argv[1]) != (128 * 128)/8 * 2): 
		print('bad length')
		print(len(sys.argv[1]))
		quit()

	bytes = np.array(list(binascii.unhexlify(hex_data)), dtype=np.uint8)
	image_bits = np.unpackbits(bytes).reshape(128, 128) * 255
	im = Image.fromarray(image_bits, mode='L')
	im.show()


if __name__ == '__main__':
	main()



# Working on that more than physics?


# 6 homeworks
# Tuesday night (19th) - Calc Homework (day before calc test)
# Thurs 20th - Calc Test

# 23rd - Chem homework due

# 25th - Chem Lecture Test
# 26th - Phys Test
# 27th - Chem Lab Test
