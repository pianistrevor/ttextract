import os, sys
from unlz2k import unlz2k
unpack = True

# Via http://www.isthe.com/chongo/tech/comp/fnv/
FNV_BASIS = 2166136261
#FNV_PRIME = 16777619
FNV_PRIME = 1677619

algs = {0: '----', 2: 'LZ2K'}

def get_extract_name(file):
    extract_name = os.path.basename(file.name)
    ext = extract_name.rfind('.')
    return extract_name[:ext]

def read_name(file, offset):
    """ Reads a string from the file at the offset (null terminated) """
    prev_offset = file.tell()
    file.seek(offset)
    item_name = ""
    char = int.from_bytes(file.read(1), "little")
    while char != 0:
        item_name += chr(char)
        char = int.from_bytes(file.read(1), "little")
    file.seek(prev_offset)
    return item_name

def handle_fpk(file, file_size):
    # Number of files to read
    num_files = int.from_bytes(file.read(4), byteorder="little")

    # Total file size
    expected_size = int.from_bytes(file.read(4), byteorder="little")
    if file_size != expected_size:
        print('Error: File size mismatch (expected {:<8X}, got {:<8X})'.format(expected_size, file_size))
        exit(0)
    for i in range(num_files):
        name_offset = int.from_bytes(file.read(4), byteorder="little")
        data_offset = int.from_bytes(file.read(4), byteorder="little")
        file_size = int.from_bytes(file.read(4), byteorder="little")
        sixteen = int.from_bytes(file.read(4), byteorder="little")
        if sixteen != 16:
            print('Error: Expected 16 but got {}'.format(sixteen))
            exit(0)
        # Read three words of nothing
        file.read(12)
        name = read_name(file, name_offset)
        # Split everything by '\' and extract directory
        directories = name.rfind('\\')
        file_dir = name[0:directories]
        # Create file
        print('{:<8X}\t{:<8X}\t{}\t{}'.format(data_offset, file_size, i, name))
        dirs_exist = os.path.exists(file_dir)
        if not dirs_exist:
            os.makedirs(file_dir)
        new_file = open(name, 'wb')
        # Jump to data offset
        prev_offset = file.tell()
        file.seek(data_offset)
        new_file.write(file.read(file_size))
        file.seek(prev_offset)
    return

def handle_dat(file, file_size, first_word):

    # Get basename
    extract_name = get_extract_name(file)

    # File info section
    
    file_info_offset = first_word
    file_info_size = int.from_bytes(file.read(4), byteorder="little")
    expected_size = file_info_offset + file_info_size
    if expected_size != file_size:
        print('Error: File size mismatch (expected {:<8X}, got {:<8X})'.format(expected_size, file_size))
        exit(1)
    file.seek(file_info_offset)
    signature = int.from_bytes(file.read(4), byteorder="little", signed=True)
    if signature not in [-1, -3]:
        print('Error: File info signature invalid.')
        exit(1)
    print('DAT file with signature: {}'.format(signature))
    num_files = int.from_bytes(file.read(4), byteorder="little")
    file_info_offset += 8
    print('File info offset: {:<8X}'.format(file_info_offset))
    print('File info size: {:<8X}'.format(file_info_size))
    print('Number of files: {}'.format(num_files))

    # Name info section

    name_info_offset = file_info_offset + (num_files * 16)
    file.seek(name_info_offset)
    num_names = int.from_bytes(file.read(4), byteorder="little")
    name_info_offset += 4
    print('Name info offset: {:<8X}'.format(name_info_offset))
    print('Number of names: {}'.format(num_names))

    # Name data section

    name_data_offset = name_info_offset + (num_names * 8)
    file.seek(name_data_offset)
    # 4 bytes preceding name data is the CRC offset.
    name_crc_offset = int.from_bytes(file.read(4), byteorder="little")
    name_data_offset += 4
    name_crc_offset += name_data_offset
    print('Name data offset: {:<8X}'.format(name_data_offset))
    print('Name CRC offset: {:<8X}'.format(name_crc_offset))

    # CRC section
    
    file_index_of_crc = dict() # Key is CRC, value is file index
    has_crcs = True
    file.seek(name_crc_offset)
    first = int.from_bytes(file.read(4), byteorder="little")
    if first == 0:
        # No CRCs
        has_crcs = False
    if has_crcs:
        file_index_of_crc[first] = 0
        for i in range(1, num_files):
            crc = int.from_bytes(file.read(4), byteorder="little")
            file_index_of_crc[crc] = i
    # Should have two words left, expecting zeroes
    temp = int.from_bytes(file.read(8), byteorder="little")
    if temp != 0:
        print("Error: File bytes at end were unexpected. ({:<8X})".format(file.tell() - 8))
        exit(1)
    extra_data = file.read()
    if extra_data != b'':
        print("Error: Unexpected end of file. ({:<8X})".format(file.tell()))
        exit(1)
    print('Number of CRCs: {}\n'.format(len(file_index_of_crc)))
    print('Offset  \tPacked  \tUnpacked\tAlg?\tFile')
    print('-' * 100)

    """ Now the extraction section!

    Name info table items are 8 bytes long.

    First value is signed 16 bit.
        If positive, read folder name. Value is index for last item in folder.
        If negative or zero, read file name. Value is 2s complement of file ID.

    Second value is signed 16 bit.
        If positive, use previous file path. Value is index of previous item written in directory
        If zero, do not use previous file path. Write item to current directory.

    Third value is relative 32-bit offset to read name from. This is added to the name data offset.
    """

    file.seek(name_info_offset)
    data_offsets = dict()
    file_sizes = dict()
    file_sizes_unpacked = dict()
    last_item_offset = 0
    last_file_id_written = 0 # Unused thus far?
    current_dir = "" # Change this to modify output dir
    item_dirs = dict()
    for name_offset in range(num_names):
        is_folder = False
        is_packed = False
        item_dir = ""
        file_id = -1
        read_type = int.from_bytes(file.read(2), byteorder="little", signed=True)
        if read_type > 0:
            last_item_offset = read_type
            is_folder = True
        else:
            file_id = read_type * -1
        path_type = int.from_bytes(file.read(2), byteorder="little", signed=True)
        if path_type > 0:
            item_dir = item_dirs.get(path_type)
        else:
            item_dir = current_dir
        item_dirs[name_offset] = item_dir
        name_data_relative_offset = int.from_bytes(f.read(4), byteorder="little")

        # Get name
        item_name = read_name(file, name_data_offset + name_data_relative_offset)
        # Append to file path
        if item_name != "": # Avoid creating double slash for blank directory
            item_dir += '\\'
        item_dir += item_name

        if is_folder:
            if item_dir != "":
                # Switch to new directory if it's a folder name
                current_dir = item_dir
        else:
            if has_crcs:
                analysis_name = item_dir[1:].upper()
                crc = FNV_BASIS
                for char in analysis_name:
                    crc = (crc ^ ord(char)) * FNV_PRIME & 0xFFFFFFFF
                # Now find the corresponding CRC index in our collection
                file_index = file_index_of_crc.get(crc)
                if file_index is None:
                    print('Error: File does not appear to have a CRC in the table.')
                    exit(1)
            else:
                # Extract current file
                file_index = file_id

            """ File info table items are 16 bytes long.

            4 bytes = absolute offset of data
                Signature -3 multiplies it by 256
            4 bytes = size of file (archived)
            4 bytes = size of file (uncompressed)
                Signature -1 expects this to match
            3 bytes = packed type
                Signature -1 expects 0
                Signature -3 expects 2 thus far
            1 byte  = fine offset from the given offset
                Signature -1 expects this to be 0
            """
            file_index = file_info_offset + file_index * 16
            prev_offset = file.tell()
            file.seek(file_index)
            if signature == -1:
                offset = int.from_bytes(file.read(4), byteorder="little")
                size_packed = int.from_bytes(file.read(4), byteorder="little")
                size_unpacked = int.from_bytes(file.read(4), byteorder="little")
                if (size_packed != size_unpacked):
                    print('Error: Unknown situation, file sizes do not match. (file {})'.format(i))
                    exit(1)
                packed_type = int.from_bytes(file.read(3), byteorder="little")
                if packed_type != 0:
                    print('Error: Unknown situation, packed type not 0. (file {})'.format(i))
                    exit(1)
                offset += int.from_bytes(file.read(1), byteorder="little")
                data_offsets[file_index] = offset
                file_sizes[file_index] = size_packed
            else:
                offset = int.from_bytes(file.read(4), byteorder="little") << 8
                size_packed = int.from_bytes(file.read(4), byteorder="little")
                size_unpacked = int.from_bytes(file.read(4), byteorder="little")
                packed_type = int.from_bytes(file.read(3), byteorder="little")
                offset += int.from_bytes(file.read(1), byteorder="little")
                if (size_packed != size_unpacked):
                    is_packed = True
                data_offsets[file_index] = offset
                # Right now we're writing compressed files because we don't have the alg
                file_sizes[file_index] = size_packed
                file_sizes_unpacked[file_index] = size_unpacked
            output_dir = extract_name + current_dir
            output_item = extract_name + item_dir
            print('{:<8X}\t{:<8X}\t{:<8X}\t{}\t{}'.format(offset, size_packed, size_unpacked, algs[packed_type], output_item))
            dirs_exist = os.path.exists(output_dir)
            if not dirs_exist:
                os.makedirs(output_dir)
            new_file = open(output_item, 'wb')
            #data_offset = data_offsets.get(file_index)
            # Jump to data offset
            #file.seek(data_offset)
            file.seek(offset)
            if unpack and is_packed:
                if packed_type == 2:
                    unlz2k(file, new_file, size_packed, size_unpacked)
                else:
                    print('Unknown packed type, unpacking raw file')
                    new_file.write(file.read(size_packed))
            else:
                new_file.write(file.read(size_packed))
            # Return to original offset
            file.seek(prev_offset)
            
def handle_default():
    print('Error: Not a recognized .DAT or .FPK file.')
    exit(1)

# First read file
file_name = str(sys.argv[1])
if '-r' in sys.argv:
    unpack = False
try:
    f = open(file_name, "rb")
except:
    print('Error opening the specified file.')
    exit(1)

f.seek(0, 2)
file_size = f.tell()
f.seek(0)

# Read first word. It will tell us what kind of file it is
first_word = int.from_bytes(f.read(4), byteorder="little")
if first_word == 0x12345678:
    handle_fpk(f, file_size)
elif first_word < file_size:
    handle_dat(f, file_size, first_word)
else:
    handle_default()


















    
