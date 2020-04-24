// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include "mt7915.h"
#include "eeprom.h"

static inline bool mt7915_efuse_valid(u8 val)
{
	return !(val == 0xff);
}

u32 mt7915_eeprom_read(struct mt7915_dev *dev, u32 offset)
{
	u8 *data = dev->mt76.eeprom.data;

	if (!mt7915_efuse_valid(data[offset]))
		mt7915_mcu_get_eeprom(dev, offset);

	return data[offset];
}

static int mt7915_eeprom_load(struct mt7915_dev *dev)
{
	int ret;

	ret = mt76_eeprom_init(&dev->mt76, MT7915_EEPROM_SIZE);
	if (ret < 0)
		return ret;

	memset(dev->mt76.eeprom.data, -1, MT7915_EEPROM_SIZE);

	return 0;
}

static int mt7915_check_eeprom(struct mt7915_dev *dev)
{
	u16 val;
	u8 *eeprom = dev->mt76.eeprom.data;

	mt7915_eeprom_read(dev, 0);
	val = get_unaligned_le16(eeprom);

	switch (val) {
	case 0x7915:
		return 0;
	default:
		return -EINVAL;
	}
}

static void mt7915_eeprom_parse_hw_cap(struct mt7915_dev *dev)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	u8 tx_mask, max_nss = 4;
	u32 val = mt7915_eeprom_read(dev, MT_EE_WIFI_CONF);

	val = FIELD_GET(MT_EE_WIFI_CONF_BAND_SEL, val);
	switch (val) {
	case MT_EE_5GHZ:
		dev->mt76.cap.has_5ghz = true;
		break;
	case MT_EE_2GHZ:
		dev->mt76.cap.has_2ghz = true;
		break;
	default:
		dev->mt76.cap.has_2ghz = true;
		dev->mt76.cap.has_5ghz = true;
		break;
	}

	/* read tx mask from eeprom */
	tx_mask =  FIELD_GET(MT_EE_WIFI_CONF_TX_MASK,
			     eeprom[MT_EE_WIFI_CONF]);
	if (!tx_mask || tx_mask > max_nss)
		tx_mask = max_nss;

	dev->chainmask = BIT(tx_mask) - 1;
	dev->mphy.antenna_mask = dev->chainmask;
	dev->phy.chainmask = dev->chainmask;
}

int mt7915_eeprom_init(struct mt7915_dev *dev)
{
	int ret;

	ret = mt7915_eeprom_load(dev);
	if (ret < 0)
		return ret;

	ret = mt7915_check_eeprom(dev);
	if (ret)
		return ret;

	mt7915_eeprom_parse_hw_cap(dev);
	memcpy(dev->mt76.macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR,
	       ETH_ALEN);

	mt76_eeprom_override(&dev->mt76);

	return 0;
}

int mt7915_eeprom_get_target_power(struct mt7915_dev *dev,
				   struct ieee80211_channel *chan,
				   u8 chain_idx)
{
	int index;
	bool tssi_on;

	if (chain_idx > 3)
		return -EINVAL;

	tssi_on = mt7915_tssi_enabled(dev, chan->band);

	if (chan->band == NL80211_BAND_2GHZ) {
		index = MT_EE_TX0_POWER_2G + chain_idx * 3 + !tssi_on;
	} else {
		int group = tssi_on ?
			    mt7915_get_channel_group(chan->hw_value) : 8;

		index = MT_EE_TX0_POWER_5G + chain_idx * 12 + group;
	}

	return mt7915_eeprom_read(dev, index);
}