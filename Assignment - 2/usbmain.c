
//////////////////////////////////// USB Driver for size/capacity estimation of Memory Device /////////////////////////

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>

//////////////////////////////////////// Specify the VID and PID of the device ////////////////////////////////////////

#define SANDISK_VID  0x0781			// Vendor ID for SanDisk Storage Device
#define SANDISK_PID  0x5567			// Product ID for Sandisk Storage Device

#define SONY_VID  0x054C			// Vendor ID for Sony Storage Device
#define SONY_PID  0x05BA			// Product ID for Sony Storage Device

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define READ_CAPACITY_LENGTH          0x08
#define LIBUSB_ENDPOINT_IN            0x80
#define LIBUSB_REQUEST_TYPE_CLASS     (0x01<<5)
#define LIBUSB_RECIPIENT_INTERFACE    0x01
#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])
#define BOMS_RESET                    0xFF
#define BOMS_RESET_REQ_TYPE           0x21
#define BOMS_GET_MAX_LUN              0xFE
#define BOMS_GET_MAX_LUN_REQ_TYPE     0xA1
#define REQUEST_DATA_LENGTH           0x12
#define LIBUSB_ERROR_PIPE             -9
#define LIBUSB_SUCCESS                0
#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])


////////////////////////////////////// Command Block Wrapper (CBW) //////////////////////////////////////////////////

struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

////////////////////////////////////// Command Status Wrapper (CSW) ////////////////////////////////////////////////

struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

static uint8_t cdb_length[256] = {
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  F
};

static void usbdev_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "\nPendrive Removed\n\n");
	return;
}

/////////////////////////////////////////// List Of Storages Devices //////////////////////////////////////////////

static struct usb_device_id usbdev_table [] = {
	{USB_DEVICE(SANDISK_VID , SANDISK_PID)},
	{USB_DEVICE(SONY_VID , SONY_PID)},
	{}	
};

//////////////////////////////////////////// Device Status ///////////////////////////////////////////////////////////

static int get_mass_storage_status(struct usb_device *device, uint8_t endpoint, uint32_t expected_tag)
{	struct command_status_wrapper *csw;
	csw=(struct command_status_wrapper *)kmalloc(sizeof(struct command_status_wrapper),GFP_KERNEL);
	int r,size;
	r=usb_bulk_msg(device,usb_rcvbulkpipe(device,endpoint),(void*)csw,13, &size, 1000);
	if(r<0)
		printk("ERROR IN RECIVING STATUS MESSAGE %d",r);
	if (size != 13) {
		printk("   get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}	
	if (csw->dCSWTag != expected_tag) {
		printk("   get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw->dCSWTag);
		return -1;
	}
	printk(KERN_INFO "\nSTATUS: %02X (%s)\n", csw->bCSWStatus, csw->bCSWStatus?"FAILED TO READ THE DEVICE":"SUCCESSFULLY READ THE DEVICE SIZE");
    if (csw->dCSWTag != expected_tag)
		return -1;
	if (csw->bCSWStatus) {
		if (csw->bCSWStatus == 1)
			return -2;
		else
			return -1;
	}	
	return 0;
}

///////////////////////////////////////////// Device Commands to read Capacity //////////////////////////////////////////////////

static int send_mass_storage_command(struct usb_device *device, uint8_t endpoint, uint8_t lun,
	uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int i=0, r, size;
	struct command_block_wrapper *cbw;
	cbw=(struct command_block_wrapper *)kmalloc(sizeof(struct command_block_wrapper),GFP_KERNEL);
	if (cdb == NULL) {
		return -1;
	}
	if (endpoint & USB_DIR_IN) {
		printk(KERN_INFO "send_mass_storage_command: cannot send command on IN endpoint\n");
		return -1;
	}
	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw->CBWCB))) {
		printk(KERN_INFO "send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",
			cdb[0], cdb_len);
		return -1;
	}
	memset(cbw, 0, sizeof(*cbw));
	cbw->dCBWSignature[0] = 'U';
	cbw->dCBWSignature[1] = 'S';
	cbw->dCBWSignature[2] = 'B';
	cbw->dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw->dCBWTag = tag++;
	cbw->dCBWDataTransferLength = data_length;
	cbw->bmCBWFlags = direction;
	cbw->bCBWLUN = lun;
	cbw->bCBWCBLength = cdb_len;
	memcpy(cbw->CBWCB, cdb, cdb_len);
	r = usb_bulk_msg(device, usb_sndbulkpipe(device,endpoint), (void*)cbw, 31, &size, 1000);
    if(r!=0)
	    printk("Failed command transfer %d",r);
    else 	
    	printk("Read Capacity command sent successfully");

    printk(KERN_INFO "sent %d CDB bytes\n", cdb_len);
    printk(KERN_INFO "sent %d bytes \n",size);

	return 0;
}

/////////////////////////////// Mass Storage device to test bulk transfers ////////////////////////////////////////////

static int test_mass_storage (struct usb_device *device , uint8_t endpoint_in, uint8_t endpoint_out)
{
	int r=0, r1=0,size,j,r2=0;
	uint8_t *lun=(uint8_t *)kmalloc(sizeof(uint8_t),GFP_KERNEL);
	uint32_t expected_tag;
	uint32_t i, max_lba, block_size;
	long device_size;
	uint8_t cdb[16];	// SCSI Command Descriptor Block
	uint8_t *buffer=(uint8_t *)kmalloc(64*sizeof(uint8_t),GFP_KERNEL);
    printk("\nReset Mass Storage Device");
	r1 = usb_control_msg(device,usb_sndctrlpipe(device,0),BOMS_RESET,BOMS_RESET_REQ_TYPE,0,0,NULL,0,1000);
	if(r1<0)
		printk("error code: %d",r1);
	else
		printk("Reset Successful\n");
	printk(KERN_INFO "\nReading Max LUN: %d\n",*lun);
	r = usb_control_msg(device,usb_sndctrlpipe(device,0), BOMS_GET_MAX_LUN ,BOMS_GET_MAX_LUN_REQ_TYPE,
	   0, 0, (void*)lun, 1, 1000);
	if (r == 0) {
		*lun = 0;
	} else if (r < 0) {
		printk(KERN_INFO "   Failed: %d\n", r);
	}
	printk(KERN_INFO "   Max LUN = %d\n", *lun);
    printk(KERN_INFO "   r = %d\n\n",r);

	/////////////////////////////////////////////// Read capacity ///////////////////////////////////////////////////

	printk(KERN_INFO "Reading Capacity:\n");
	memset(buffer, 0, sizeof(buffer));
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = 0x25;	// Read Capacity
	send_mass_storage_command(device, endpoint_out, *lun, cdb, USB_DIR_IN, READ_CAPACITY_LENGTH, &expected_tag);
	r2 = usb_bulk_msg(device, usb_rcvbulkpipe(device,endpoint_in), (void*)buffer, 64, &size, 10000);
	if(r2<0)
		printk(KERN_INFO "status of r2 %d",r2);
	printk(KERN_INFO "received %d bytes\n", size);
	printk(KERN_INFO"Value of &bufer[0] =  %d\n",buffer[0]);
	max_lba = be_to_int32(&buffer[0]);
	block_size = be_to_int32(&buffer[4]);
	device_size = ((long)(max_lba+1))*block_size/(1024*1024);
	printk("Max_LBA: %x \n",max_lba);
	printk("Block_Size: %x \n",block_size);
	printk(KERN_INFO "\n***** DEVICE SIZE/CAPACITY: %d MB / %d GB *****\n",device_size,device_size/1024);
	get_mass_storage_status(device, endpoint_in, expected_tag);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int usbdev_probe(struct usb_interface *interface, const struct usb_device_id *id)
{   
	struct usb_device *device;
	device = interface_to_usbdev(interface);
	uint8_t bus, port_path[8];
	struct usb_endpoint_descriptor *endpoint;
	unsigned char epAddr, epAttr;
	int i, j, k, r;
	int iface, nb_ifaces, first_iface = -1;
	uint8_t endpoint_in = 0, endpoint_out = 0;	// default IN and OUT endpoints
    if((id->idProduct == SANDISK_PID && id->idVendor==SANDISK_VID) || (id->idProduct == SONY_PID && id->idVendor==SONY_VID))
	{
		printk(KERN_INFO "\nUSB Drive Detected\n");
	}
	printk(KERN_INFO "\nReading Device Descriptor:\n");
	printk(KERN_INFO "USB Vendor ID: %04X\n",id->idVendor);
	printk(KERN_INFO "USB Product ID: %04X\n",id->idProduct);
	printk(KERN_INFO "USB DEVICE CLASS : %x", interface->cur_altsetting->desc.bInterfaceClass);
	printk(KERN_INFO "USB DEVICE SUB CLASS : %x", interface->cur_altsetting->desc.bInterfaceSubClass);
	printk(KERN_INFO "USB DEVICE Protocol : %x", interface->cur_altsetting->desc.bInterfaceProtocol);
	nb_ifaces = device->config->desc.bNumInterfaces;
	printk(KERN_INFO "nb interfaces: %d\n", nb_ifaces);
	// Check if the device is USB attaced SCSI type Mass storage class
	if ( (interface->cur_altsetting->desc.bInterfaceClass == 0x08)
	  && (interface->cur_altsetting->desc.bInterfaceSubClass == 0x06) 
	  && (interface->cur_altsetting->desc.bInterfaceProtocol == 0x50) )
	{  
		printk(KERN_INFO "Valid SCSI device");
	}
	printk(KERN_INFO "\nEndpoints = %d\n",interface->cur_altsetting->desc.bNumEndpoints);
	for (k=0; k<interface->cur_altsetting->desc.bNumEndpoints; k++) 
	{
		endpoint = &interface->cur_altsetting->endpoint[k].desc;
		printk(KERN_INFO "endpoint[%d].address: %02X\n", k, endpoint->bEndpointAddress);
		
	 	epAddr = endpoint->bEndpointAddress;
    	epAttr = endpoint->bmAttributes;

        if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_BULK)
        {
	       if(epAddr & 0x80)///masking d7 bit of bEndpointAddres and checkiing if its 1 then endpoint dir is in else out
		     {   
		     	 endpoint_in= endpoint->bEndpointAddress;
		         printk(KERN_INFO "EP %d is Bulk IN\n", k);
		     }
	       else
		      {   
		      	  endpoint_out= endpoint->bEndpointAddress;
			      printk(KERN_INFO "EP %d is Bulk OUT\n", k);
		      }

        } 

		printk(KERN_INFO "		Max Packet Size: %04X\n", endpoint->wMaxPacketSize);
		printk(KERN_INFO "		Polling interval: %02X\n", endpoint->bInterval);
		
	}
        test_mass_storage(device, endpoint_in, endpoint_out);
	return 0;
}

////////////////////////////////////////////// Operations structure /////////////////////////////////////////////////

static struct usb_driver usbdev_driver = {
	name: "usbdev",  					//name of the device
	probe: usbdev_probe, 				// Whenever Device is plugged in
	disconnect: usbdev_disconnect, 		// When we remove a device
	id_table: usbdev_table, 			//  List of devices served by this driver
};

////////////////////////////////////////////////// init Module //////////////////////////////////////////////////////

int USB_init(void)
{
	usb_register(&usbdev_driver);
	printk(KERN_INFO "Mass Storage Driver Registered");
	return 0;
}

////////////////////////////////////////////////// exit module /////////////////////////////////////////////////////

void USB_exit(void)
{
	usb_deregister(&usbdev_driver);
	printk(KERN_NOTICE "Mass Storage Driver removed from Kernel \n");
}

module_init(USB_init);
module_exit(USB_exit);

MODULE_DESCRIPTION("USB module");
MODULE_AUTHOR("Tushar patil <tusharpatil10397@gmail.com>");
MODULE_LICENSE("GPL");

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
