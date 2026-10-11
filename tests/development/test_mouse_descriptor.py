"""Check the actual configfs HID descriptor against the bridge report layouts."""
import pathlib,re,sys,tempfile,os,subprocess
from unittest import mock

script=(pathlib.Path(__file__).resolve().parents[2]/'scripts/development/usb-gadget.sh').read_text()
encoded=re.search(r"write_descriptor '(.*?)' functions/hid.usb1/report_desc",script).group(1)
descriptor=bytes.fromhex(encoded.replace('\\x',''))
# Exercise the script's actual writer. configfs replaces content on every
# write, so the entire binary descriptor must be submitted in one call.
writer=re.search(r"python3 - .*? <<'PY'\n(.*?)\nPY",script,re.S).group(1)
with tempfile.TemporaryDirectory() as directory:
    target=pathlib.Path(directory)/'report_desc'
    target.touch()
    with mock.patch.object(sys,'argv',['writer',str(target),encoded]),mock.patch('os.write',wraps=os.write) as write:
        exec(compile(writer,'usb-gadget-descriptor-writer','exec'),{})
        assert write.call_count==1
        assert write.call_args.args[1]==descriptor
    assert target.read_bytes()==descriptor
    # Run the real Bash wrapper with the argument order used by the boot script.
    # The Python writer alone cannot detect a wrapper passing its args backward.
    function=re.search(r'(write_descriptor\(\) \{.*?\n\})',script,re.S).group(1)
    target.write_bytes(b'')
    subprocess.run(['bash','-c',function+'\nwrite_descriptor "$1" "$2"','test',encoded,str(target)],check=True)
    assert target.read_bytes()==descriptor
size=count=report_id=0
input_bits={}
index=0
while index<len(descriptor):
    prefix=descriptor[index];index+=1
    assert prefix!=0xfe,'Unexpected HID long item'
    length=prefix&3
    if length==3:length=4
    data=descriptor[index:index+length];assert len(data)==length
    index+=length
    value=int.from_bytes(data,'little')
    kind=(prefix>>2)&3;tag=prefix>>4
    if kind==1:
        if tag==7:size=value
        if tag==8:report_id=value
        if tag==9:count=value
    elif kind==0 and tag==8:
        input_bits[report_id]=input_bits.get(report_id,0)+size*count
assert input_bits=={1:64,2:64,3:16},input_bits
assert all((bits+7)//8+1<=9 for bits in input_bits.values())
assert 'echo 1 > functions/hid.usb1/interval' in script
assert 'echo 1 > functions/hid.usb1/no_out_endpoint' in script
print(f'USB descriptor: {len(descriptor)} bytes in one write; mouse 9 bytes, keyboard 9 bytes, consumer 3 bytes; bInterval=1, IN-only')
