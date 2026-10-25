import sys

if not len(sys.argv):
    print("[!] pass file name as an argument.")
    sys.exit()

filename = sys.argv[1]
file_contents = []
with open(filename, "rb") as file:
    file_contents = file.read(640 * 480)

retransformed_map = []
for i in range(640 * 480):
    byte = file_contents[i]

    red = byte & 0x3
    green = (byte >> 2) & 0x3
    blue = (byte >> 4) & 0x3

    red_byte = red * 0x55
    green_byte = green * 0x55
    blue_byte = blue * 0x55

    retransformed_map.append(red_byte)
    retransformed_map.append(green_byte)
    retransformed_map.append(blue_byte)

with open("retransformed.ppm", "wb") as file:
    header = "P6\n640\n480\n255\n"
    file.write(str.encode(header))

    for byte in retransformed_map:
        file.write(byte.to_bytes(1, byteorder="big"))
