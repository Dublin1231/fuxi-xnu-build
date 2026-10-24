import unittest
from hid_review import descriptor_contract

class DescriptorTests(unittest.TestCase):
    def descriptor(self):
        return ('#define FINGER_DESC \\\n0x75,0x08,0x95,0x06,0x81,0x02,\n'
            'static const unsigned char touchDescriptor[]={\n'
            '0x05,0x0d,0x09,0x04,0xa1,0x01,0x85,0x03,\n'+
            ' FINGER_DESC'*10+'\n0x75,0x08,0x95,0x01,0x81,0x02,0xc0\n};')
    def test_exact_report_size_and_primary_usage(self):
        self.assertEqual(descriptor_contract(self.descriptor())['bytes_including_report_id'],62)
    def test_changed_id_size_usage_or_truncation_rejected(self):
        text=self.descriptor()
        for bad in [text.replace('0x85,0x03','0x85,0x02'),text.replace('0x95,0x06','0x95,0x05'),
                    text.replace('0x09,0x04','0x09,0x06'),text.replace('0xc0','0x76')]:
            with self.subTest(bad=bad),self.assertRaises(ValueError):descriptor_contract(bad)

if __name__=='__main__':unittest.main()
