/*
 * Copyright (C) 2009 Google, Inc.
 * Copyright (C) 2009 HTC Corporation
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/uaccess.h>

#include <linux/msm_audio.h>
#include <linux/msm_audio_amrnb.h>
#include <mach/msm_qdsp6_audio.h>
#include "dal_audio_format.h"
#include <mach/debug_mm.h>

struct amrnb {
	struct mutex lock;
	struct msm_audio_amrnb_enc_config_v2 cfg;
	struct msm_audio_stream_config str_cfg;
	struct audio_client *audio_client;
	struct msm_voicerec_mode voicerec_mode;
};


static long q6_amrnb_in_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct amrnb *amrnb = file->private_data;
	int rc = 0;

	mutex_lock(&amrnb->lock);
	switch (cmd) {
	case AUDIO_SET_VOLUME:
		pr_debug("[%s:%s] SET_VOLUME\n", __MM_FILE__, __func__);
		break;
	case AUDIO_GET_STATS:
	{
		struct msm_audio_stats stats;
		pr_debug("[%s:%s] GET_STATS\n", __MM_FILE__, __func__);
		memset(&stats, 0, sizeof(stats));
		if (copy_to_user((void *) arg, &stats, sizeof(stats)))
			return -EFAULT;
		return 0;
	}
	case AUDIO_START:
	{
		uint32_t acdb_id;
		pr_debug("[%s:%s] AUDIO_START\n", __MM_FILE__, __func__);
		if (arg == 0) {
			acdb_id = 0;
		} else {
			if (copy_from_user(&acdb_id, (void *) arg,
						sizeof(acdb_id))) {
				rc = -EFAULT;
				break;
			}
		}
		if (amrnb->audio_client) {
			rc = -EBUSY;
			pr_err("[%s:%s] active session already existing\n",
				__MM_FILE__, __func__);
			break;
		} else {
			amrnb->audio_client = q6audio_open_amrnb(
					amrnb->str_cfg.buffer_size,
					amrnb->cfg.band_mode,
					amrnb->cfg.dtx_enable,
					amrnb->voicerec_mode.rec_mode,
					acdb_id);
			if (!amrnb->audio_client) {
				pr_err("[%s:%s] amrnb open session failed\n",
					__MM_FILE__, __func__);
				kfree(amrnb);
				rc = -ENOMEM;
				break;
			}
		}
		break;
	}
	case AUDIO_STOP:
		pr_debug("[%s:%s] AUDIO_STOP\n", __MM_FILE__, __func__);
		break;
	case AUDIO_FLUSH:
		break;
	case AUDIO_SET_INCALL: {
		pr_debug("[%s:%s] SET_INCALL\n", __MM_FILE__, __func__);
		if (copy_from_user(&amrnb->voicerec_mode,
			(void *)arg, sizeof(struct msm_voicerec_mode)))
			rc = -EFAULT;

		if (amrnb->voicerec_mode.rec_mode != AUDIO_FLAG_READ
				&& amrnb->voicerec_mode.rec_mode !=
				AUDIO_FLAG_INCALL_MIXED) {
			amrnb->voicerec_mode.rec_mode = AUDIO_FLAG_READ;
			pr_err("[%s:%s] Invalid rec_mode\n", __MM_FILE__,
					__func__);
			rc = -EINVAL;
		}
		break;
	}
	case AUDIO_GET_STREAM_CONFIG:
		if (copy_to_user((void *)arg, &amrnb->str_cfg,
			sizeof(struct msm_audio_stream_config)))
			rc = -EFAULT;
		pr_debug("[%s:%s] GET_STREAM_CONFIG: buffsz=%d, buffcnt = %d\n",
			 __MM_FILE__, __func__, amrnb->str_cfg.buffer_size,
			amrnb->str_cfg.buffer_count);
		break;
	case AUDIO_SET_STREAM_CONFIG:
		if (copy_from_user(&amrnb->str_cfg, (void *)arg,
			sizeof(struct msm_audio_stream_config))) {
			rc = -EFAULT;
			break;
		}
		pr_debug("[%s:%s] SET_STREAM_CONFIG: buffsz=%d, buffcnt = %d\n",
			 __MM_FILE__, __func__, amrnb->str_cfg.buffer_size,
			amrnb->str_cfg.buffer_count);

		if (amrnb->str_cfg.buffer_size < 768) {
			pr_err("[%s:%s] Buffer size too small\n", __MM_FILE__,
					__func__);
			rc = -EINVAL;
			break;
		}

		if (amrnb->str_cfg.buffer_count != 2)
			pr_info("[%s:%s] Buffer count set to 2\n", __MM_FILE__,
					__func__);
		break;
	case AUDIO_SET_AMRNB_ENC_CONFIG:
		if (copy_from_user(&amrnb->cfg, (void *) arg,
			sizeof(struct msm_audio_amrnb_enc_config_v2)))
			rc = -EFAULT;
		pr_debug("[%s:%s] SET_AMRNB_ENC_CONFIG\n", __MM_FILE__,
			__func__);
		break;
	case AUDIO_GET_AMRNB_ENC_CONFIG:
		if (copy_to_user((void *) arg, &amrnb->cfg,
				 sizeof(struct msm_audio_amrnb_enc_config_v2)))
			rc = -EFAULT;
		pr_debug("[%s:%s] GET_AMRNB_ENC_CONFIG\n", __MM_FILE__,
			__func__);
		break;

	default:
		rc = -EINVAL;
	}

	mutex_unlock(&amrnb->lock);
	pr_debug("[%s:%s] rc= %d\n", __MM_FILE__, __func__, rc);
	return rc;
}

static int q6_amrnb_in_open(struct inode *inode, struct file *file)
{
	struct amrnb *amrnb;

	pr_info("[%s:%s] open\n", __MM_FILE__, __func__);
	amrnb = kmalloc(sizeof(struct amrnb), GFP_KERNEL);
	if (amrnb == NULL) {
		pr_err("[%s:%s] Could not allocate memory for amrnb driver\n",
				__MM_FILE__, __func__);
		return -ENOMEM;
	}

	mutex_init(&amrnb->lock);
	file->private_data = amrnb;
	amrnb->audio_client = NULL;
	amrnb->str_cfg.buffer_size = 768;
	amrnb->str_cfg.buffer_count = 2;
	amrnb->cfg.band_mode = 7;
	amrnb->cfg.dtx_enable  = 3;
	amrnb->cfg.frame_format = ADSP_AUDIO_FORMAT_AMRNB_FS;
	amrnb->voicerec_mode.rec_mode = AUDIO_FLAG_READ;

	return 0;
}

static ssize_t q6_amrnb_in_read(struct file *file, char __user *buf,
			   size_t count, loff_t *pos)
{
	struct audio_client *ac;
	struct audio_buffer *ab;
	const char __user *start = buf;
	struct amrnb *amrnb = file->private_data;
	int xfer = 0;
	int res;

	pr_debug("[%s:%s] count = %d\n", __MM_FILE__, __func__, count);
	mutex_lock(&amrnb->lock);
	ac = amrnb->audio_client;
	if (!ac) {
		res = -ENODEV;
		goto fail;
	}
	while (count > xfer) {
		ab = ac->buf + ac->cpu_buf;

		if (ab->used)
			wait_event(ac->wait, (ab->used == 0));

		pr_debug("[%s:%s] ab->data = %p, cpu_buf = %d\n", __MM_FILE__,
			__func__, ab->data, ac->cpu_buf);
		xfer = ab->actual_size;

		if (copy_to_user(buf, ab->data, xfer)) {
			pr_err("[%s:%s] copy_to_user failed\n",
				__MM_FILE__, __func__);
			res = -EFAULT;
			goto fail;
		}

		buf += xfer;
		count -= xfer;

		ab->used = 1;
		q6audio_read(ac, ab);
		ac->cpu_buf ^= 1;
	}

	res = buf - start;
fail:
	mutex_unlock(&amrnb->lock);

	return res;
}

static int q6_amrnb_in_release(struct inode *inode, struct file *file)
{
	int rc = 0;
	struct amrnb *amrnb = file->private_data;

	mutex_lock(&amrnb->lock);
	if (amrnb->audio_client)
		rc = q6audio_close(amrnb->audio_client);
	mutex_unlock(&amrnb->lock);
	kfree(amrnb);
	pr_info("[%s:%s] release\n", __MM_FILE__, __func__);
	return rc;
}

static const struct file_operations q6_amrnb_in_fops = {
	.owner		= THIS_MODULE,
	.open		= q6_amrnb_in_open,
	.read		= q6_amrnb_in_read,
	.release	= q6_amrnb_in_release,
	.unlocked_ioctl	= q6_amrnb_in_ioctl,
};

struct miscdevice q6_amrnb_in_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "msm_amr_in",
	.fops	= &q6_amrnb_in_fops,
};

static int __init q6_amrnb_in_init(void)
{
	return misc_register(&q6_amrnb_in_misc);
}

device_initcall(q6_amrnb_in_init);
