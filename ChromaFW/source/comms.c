#define __packed
#include "../../../arm/einkTags/patchedfw/proto.h"
#include "asmUtil.h"
#include "printf.h"
#include "radio.h"
#include "comms.h"

#define ADDR_MODE_NONE					(0)
#define ADDR_MODE_SHORT					(2)
#define ADDR_MODE_LONG					(3)

#define FRAME_TYPE_BEACON				(0)
#define FRAME_TYPE_DATA					(1)
#define FRAME_TYPE_ACK					(2)
#define FRAME_TYPE_MAC_CMD				(3)


static uint8_t __xdata mCommsBuf[127];
static uint8_t __xdata mSeq = 0;
static uint8_t __xdata mLastLqi = 0;
static int8_t __xdata mLastRSSI = 0;


struct MacFrameFromMaster {
	struct MacFcs fcs;
	uint8_t seq;
	uint16_t pan;
	uint8_t dst[8];
	uint16_t from;
};

struct MacFrameNormal {
	struct MacFcs fcs;
	uint8_t seq;
	uint16_t pan;
	uint8_t dst[8];
	uint8_t src[8];
};

struct MacFrameBcast {
	struct MacFcs fcs;
	uint8_t seq;
	uint16_t dstPan;
	uint16_t dstAddr;
	uint16_t srcPan;
	uint8_t src[8];
};

uint8_t commsGetLastPacketLQI(void)
{
	return mLastLqi;
}

int8_t commsGetLastPacketRSSI(void)
{
	return mLastRSSI;
}

bool commsTx(struct CommsInfo *info, bool bcast, const void __xdata *packetP, uint8_t len)
{
	const uint8_t __xdata *packet = (const uint8_t __xdata*)packetP;
	struct AesCcmInfo __xdata ccmNfo = {0, };
	struct MacFrameNormal __xdata *mfn;
	struct MacFrameBcast __xdata *mfb;
	uint8_t __xdata *payload;
	uint8_t hdrSz;
	
	
	if (len > COMMS_MAX_PACKET_SZ)
		return false;
	
	if (bcast) {
		mfb = (struct MacFrameBcast __xdata*)(mCommsBuf + 1);
		xMemSet(mfb, 0, sizeof(*mfb));
		payload = (uint8_t __xdata*)(mfb + 1);
		hdrSz = sizeof(struct MacFrameBcast);
		
		mfb->fcs.frameType = FRAME_TYPE_DATA;
		mfb->fcs.destAddrType = ADDR_MODE_SHORT;
		mfb->fcs.srcAddrType = ADDR_MODE_LONG;
		
		mfb->seq = mSeq++;
		mfb->dstPan = 0xffff;
		mfb->dstAddr = 0xffff;
		mfb->srcPan = PROTO_PAN_ID;
		xMemCopy(mfb->src, info->myMac, 8);
	}
	else {
		mfn = (struct MacFrameNormal __xdata*)(mCommsBuf + 1);
		xMemSet(mfn, 0, sizeof(*mfn));
		hdrSz = sizeof(struct MacFrameNormal);
		payload = (uint8_t __xdata*)(mfn + 1);
		
		mfn->fcs.frameType = FRAME_TYPE_DATA;
		mfn->fcs.panIdCompressed = 1;
		mfn->fcs.destAddrType = ADDR_MODE_LONG;
		mfn->fcs.srcAddrType = ADDR_MODE_LONG;
		
		mfn->seq = mSeq++;
		mfn->pan = PROTO_PAN_ID;
		xMemCopy(mfn->dst, info->masterMac, 8);
		xMemCopy(mfn->src, info->myMac, 8);
	}
	
	*(uint32_t __xdata*)ccmNfo.nonce = (*info->nextIV)++;
	xMemCopy(ccmNfo.nonce + sizeof(uint32_t), info->myMac, 8);
	xMemCopy(payload, packet, len);
	
	ccmNfo.authSrcLen = hdrSz;
	ccmNfo.encDataLen = len;
	ccmNfo.key = info->encrKey;
	
	aesCcmEnc(mCommsBuf + 1, mCommsBuf + 1, &ccmNfo);
	xMemCopy(payload + len + AES_CCM_MIC_SIZE, ccmNfo.nonce, 4);	//send nonce
	
	len += hdrSz;
	len += AES_CCM_MIC_SIZE;
	len += sizeof(uint32_t);		//nonce
	mCommsBuf[0] = len;
	
	radioTx(mCommsBuf);
	
	return true;
}

int8_t commsRx(struct CommsInfo *info, void __xdata *data, uint8_t __xdata *fromMacP)
{
	uint8_t __xdata *dstData = (uint8_t __xdata *)data;
	int8_t ret = COMMS_RX_ERR_INVALID_PACKET;
	struct AesCcmInfo __xdata ccmNfo = {0, };
	struct MacFrameFromMaster __xdata *mfm;
	uint8_t len, minNeedLen, hdrLen = 0;
	struct MacFrameNormal __xdata *mfn;
	uint8_t __xdata *buf = mCommsBuf;
	uint8_t __xdata fromMac[8];
	uint8_t __xdata * __xdata rxedBuf;
	int8_t rxedLen;
	rxedLen = radioRxDequeuePktGet(&rxedBuf, &mLastLqi, &mLastRSSI);

	if (rxedLen < 0)
		return COMMS_RX_ERR_NO_PACKETS;
	len = rxedLen;
	
	//sort out how many bytes minimum are a valid packet
	minNeedLen = sizeof(struct MacFrameFromMaster);	//mac header
	minNeedLen += sizeof(uint8_t);					//packet type
	minNeedLen += AES_CCM_MIC_SIZE;					//MIC
	minNeedLen += sizeof(uint32_t);					//nonce counter
	minNeedLen += 2 * sizeof(uint8_t);				//RSSI/LQI
	
	
	//some basic checks
	mfm = (struct MacFrameFromMaster __xdata*)rxedBuf;
	if (len >= sizeof(mCommsBuf) || len < minNeedLen || mfm->fcs.frameType != FRAME_TYPE_DATA ||
			mfm->fcs.secure || mfm->fcs.frameVer || mfm->fcs.destAddrType != ADDR_MODE_LONG || !mfm->fcs.panIdCompressed ||
			(mfm->fcs.srcAddrType != ADDR_MODE_LONG && mfm->fcs.srcAddrType != ADDR_MODE_SHORT) ||
			mfm->pan != PROTO_PAN_ID || !xMemEqual(mfm->dst, info->myMac, 8)) {
		radioRxDequeuedPktRelease();
		return COMMS_RX_ERR_INVALID_PACKET;
	}
	
	//copy out and release buffer
	xMemCopy(buf, rxedBuf, len);
	radioRxDequeuedPktRelease();
	mfm = (struct MacFrameFromMaster __xdata*)buf;
	mfn = (struct MacFrameNormal __xdata*)buf;
	
	//sort out header len, copy mac into nonce
	if (mfm->fcs.srcAddrType == ADDR_MODE_LONG) {
		
		xMemCopy(fromMac, mfn->src, 8);
		hdrLen = sizeof(struct MacFrameNormal);
		
		//re-verify needed length
		minNeedLen -= sizeof(struct MacFrameFromMaster);
		minNeedLen += sizeof(struct MacFrameNormal);
		
		if (len < minNeedLen)
			return COMMS_RX_ERR_INVALID_PACKET;
	}
	else if (mfm->fcs.srcAddrType == ADDR_MODE_SHORT) {
		
		xMemCopy(fromMac, info->masterMac, 8);
		hdrLen = sizeof(struct MacFrameFromMaster);
	}
	
	//sort out the nonce
	xMemCopy(ccmNfo.nonce + sizeof(uint32_t), fromMac, 8);
	xMemCopy(ccmNfo.nonce, buf + len - sizeof(uint32_t), 4);
	
	//decrypt and auth
	len -= hdrLen + AES_CCM_MIC_SIZE + sizeof(uint32_t);
	
	ccmNfo.authSrcLen = hdrLen;
	ccmNfo.encDataLen = len;
	ccmNfo.key = info->encrKey;
	
	if (!aesCcmDec(buf, buf, &ccmNfo))
		return COMMS_RX_ERR_MIC_FAIL;
	
	if (fromMacP)
		xMemCopy(fromMacP, fromMac, 8);
	
	xMemCopy(dstData, buf + hdrLen, len);
	
	return len;
}


