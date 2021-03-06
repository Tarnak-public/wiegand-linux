/* wiegand-gpio.c
 *
 * Wiegand driver using GPIO an interrupts.
 *
 */

/* Standard headers for LKMs */
#include <linux/module.h>	/* Needed by all modules */
#include <linux/kernel.h>	/* Needed for KERN_INFO */
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/timer.h>

#include <linux/tty.h>      /* console_print() interface */
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <asm/irq.h>
#include <linux/gpio.h>
#define MAX_WIEGAND_BYTES 6
#define MIN_PULSE_INTERVAL_USEC 700

#define GPIO_WIEGAND_D0 6
#define GPIO_WIEGAND_D1 16
static struct wiegand
{
  int startParity;
  char buffer[MAX_WIEGAND_BYTES];
  int currentBit;


  int readNum;
  unsigned int lastFacilityCode;
  unsigned int lastCardNumber;
  bool lastDecoded;
  char lastBuffer[MAX_WIEGAND_BYTES];
  int numBits;

}
wiegand;


static struct timer_list timer;

static int printbinary(char *buf, unsigned long x, int nbits)
{
	unsigned long mask = 1UL << (nbits - 1);
	while (mask != 0) {
		*buf++ = (mask & x ? '1' : '0');
		mask >>= 1;
	}
	*buf = '\0';

	return nbits;
}

void print_wiegand_data(char* output, char* buf, int nbits) {
  int numBytes = ((nbits -1) / 8 ) + 1;
  int i;

  for (i=0; i< numBytes; ++i) {
		if (i == (numBytes - 1)) {
				printbinary(output, buf[i] >> ((i + 1) * 8 - nbits),  nbits - i * 8);
				output += nbits - i * 8;
		} else {
			printbinary(output, buf[i], 8);
			output += 8;
		}
	}
}


static ssize_t wiegandShow(
  struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	static char wiegand_buf[MAX_WIEGAND_BYTES * 8];
	print_wiegand_data(wiegand_buf, wiegand.lastBuffer, wiegand.numBits);


  return sprintf(
    buf, "%.5d:%s\n",
    wiegand.readNum,
    wiegand_buf
  );
}

static struct kobj_attribute wiegand_attribute =
  __ATTR(read, 0660, wiegandShow, NULL);

static struct attribute *attrs[] =
{
  &wiegand_attribute.attr,
  NULL,   /* need to NULL terminate the list of attributes */
};

static struct attribute_group attr_group =
{
  .attrs = attrs,
};

static struct kobject *wiegandKObj;

irqreturn_t wiegand_data_isr(int irq, void *dev_id);

void wiegand_clear(struct wiegand *w)
{
  w->currentBit = 0;
  w->startParity = 0;
  memset(w->buffer, 0, MAX_WIEGAND_BYTES);
}

void wiegand_init(struct wiegand *w)
{
  w->lastFacilityCode = 0;
  w->lastCardNumber = 0;
  w->readNum = 0;
  wiegand_clear(w);
}

//returns true if even parity
bool checkParity(char *buffer, int numBytes, int parityCheck)
{
  int byte = 0;
  int bit = 0;
  int mask;
  int parity = parityCheck;

  for (byte = 0; byte < numBytes; byte++)
  {
    mask = 0x80;
    for (bit = 0; bit < 8; bit++)
    {
      if (mask & buffer[byte])
      {
        parity++;
      }
      mask >>= 1;
    }
  }
  return (parity % 2) == 1;
}


void wiegand_timer(unsigned long data)
{
  char *lcn;
  char buf[MAX_WIEGAND_BYTES * 8];
  size_t i;
  struct wiegand *w = (struct wiegand *) data;
  int numBytes = ((w->currentBit -1) / 8 )+ 1;
  //~ int endParity = w->buffer[w->currentBit / 8] & (0x80 >> w->currentBit % 8);

  printk("wiegand read complete\n");



	for (i=0; i< numBytes; ++i){
		w->lastBuffer[i] = w->buffer[i];
	}

	w->numBits = w->currentBit;
	w->lastDecoded = false;
  w->readNum++;

	print_wiegand_data(buf, w->buffer, w->numBits);
  printk("new read available [%d]: %s\n",  w->numBits, buf);




  //check the start parity
  //~ if (checkParity(w->buffer, numBytes, w->startParity))
  //~ {
    //~ printk("start parity check failed\n");
    //~ wiegand_clear(w);
    //~ return;
  //~ }
//~
  //~ //check the end parity
  //~ if (!checkParity(&w->buffer[numBytes], numBytes, endParity))
  //~ {
    //~ printk("end parity check failed\n");
    //~ wiegand_clear(w);
    //~ return;
  //~ }


  //~ w->lastDecoded = true;
  //~ //ok all good set facility code and card code
  //~ w->lastFacilityCode = (unsigned int)w->buffer[0];
//~
  //~ //note relies on 32 bit architecture
  //~ w->lastCardNumber = 0;
  //~ lcn = (char *)&w->lastCardNumber;
  //~ lcn[0] = w->buffer[2];
  //~ lcn[1] = w->buffer[1];
//~
  //~ printk(
    //~ "decoded data: %d:%d\n",
    //~ w->lastFacilityCode,
    //~ w->lastCardNumber);


  //reset for next reading
  wiegand_clear(w);
}

static int irq_d0, irq_d1;

int init_module()
{
  int retval, ret;

  printk("wiegand intialising\n");

  wiegand_init(&wiegand);


  ret = gpio_request(GPIO_WIEGAND_D0, "wiegand-d0");
  if (ret)
      return ret;

  ret = gpio_request(GPIO_WIEGAND_D1, "wiegand-d1");
  if (ret)
      return ret;

  ret = gpio_direction_input(GPIO_WIEGAND_D0);
  if (ret)
      return ret;

  ret = gpio_direction_input(GPIO_WIEGAND_D1);
  if (ret)
      return ret;


	irq_d0 = gpio_to_irq(GPIO_WIEGAND_D0);
	if (irq_d0 < 0) {
		printk("can't request irq for D0 gpio\n");
		return irq_d0;
	}

	irq_d1 = gpio_to_irq(GPIO_WIEGAND_D1);
	if (irq_d1 < 0) {
		printk("can't request irq for D1 gpio\n");
		return irq_d1;
	}


  /** Request IRQ for pin */
  if(request_any_context_irq(irq_d0, wiegand_data_isr, IRQF_SHARED | IRQF_TRIGGER_FALLING, "wiegand_data", &wiegand))
  {
    printk(KERN_DEBUG"Can't register IRQ %d\n", irq_d0);
    return -EIO;
  }

  if(request_any_context_irq(irq_d1, wiegand_data_isr, IRQF_SHARED | IRQF_TRIGGER_FALLING, "wiegand_data", &wiegand))
  {
    printk(KERN_DEBUG"Can't register IRQ %d\n", irq_d1);
    return -EIO;
  }

  //setup the sysfs
  wiegandKObj = kobject_create_and_add("wiegand", kernel_kobj);

  if (!wiegandKObj)
  {
    printk("wiegand failed to create sysfs\n");
    return -ENOMEM;
  }

  retval = sysfs_create_group(wiegandKObj, &attr_group);
  if (retval)
  {
    kobject_put(wiegandKObj);
  }

  //setup the timer
  init_timer(&timer);
  timer.function = wiegand_timer;
  timer.data = (unsigned long) &wiegand;


  printk("wiegand ready\n");
  return retval;
}

irqreturn_t wiegand_data_isr(int irq, void *dev_id)
{
  struct wiegand *w = (struct wiegand *)dev_id;
  struct timespec ts, interval;
  static struct timespec lastts;
  int value = (irq == irq_d1) ? 0x80 : 0;

  getnstimeofday(&ts);
  interval = timespec_sub(ts,lastts);
  lastts = ts;
  if ((interval.tv_sec == 0 ) && (interval.tv_nsec < MIN_PULSE_INTERVAL_USEC * 1000)) {
	  return IRQ_HANDLED;
  }



//~
  //~ int data0 = gpio_get_value(GPIO_WIEGAND_D0);
  //~ int data1 = gpio_get_value(GPIO_WIEGAND_D1);
  //~ int value = ((data0 == 1) && (data1 == 0)) ? 0x80 : 0;

  //~ if ((data0 == 1) && (data1 == 1))
  //~ { //rising edge, ignore
    //~ return IRQ_HANDLED;
  //~ }

  //stop the end of transfer timer
  del_timer(&timer);

	if (w->currentBit <=  MAX_WIEGAND_BYTES * 8) {
		w->buffer[(w->currentBit) / 8] |= (value >> ((w->currentBit) % 8));
	}

  w->currentBit++;

  //if we don't get another interrupt for 50ms we
  //assume the data is complete.
  timer.expires = jiffies + msecs_to_jiffies(50);
  add_timer(&timer);

  return IRQ_HANDLED;
}

void cleanup_module()
{
  kobject_put(wiegandKObj);
  del_timer(&timer);

  free_irq(irq_d0, &wiegand);
  free_irq(irq_d1, &wiegand);

  gpio_free(GPIO_WIEGAND_D0);
  gpio_free(GPIO_WIEGAND_D1);



  printk("wiegand removed\n");
}

MODULE_DESCRIPTION("Wiegand GPIO driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("VerveWorks Pty. Ltd.");


