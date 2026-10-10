"""Check the actual configfs HID descriptor against the bridge report layouts."""
import pathlib,re

script=(pathlib.Path(__file__).resolve().parents[2]/'scripts/development/usb-gadget.sh').read_text()
encoded=re.search(r"printf '%b' '(.*?)' > functions/hid.usb1/report_desc",script).group(1)
descriptor=bytes.fromhex(encoded.replace('\\x',''))
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
print('Actual USB HID descriptor: mouse 9 bytes, keyboard 9 bytes, consumer 3 bytes; bInterval=1, IN-only')
