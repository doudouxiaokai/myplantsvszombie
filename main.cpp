#define _CRT_SECURE_NO_WARNINGS
#include "tools.h"
#include <graphics.h>
#include <objidl.h>
#include <gdiplus.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdiplus.lib")

#define WIN_WIDTH 1050
#define WIN_HEIGHT 600

/* ============================================================
 *                        可调参数
 *  数值都集中在这里，想改平衡性只改这一块即可
 * ============================================================ */
#define PEA_DAMAGE           20     // 每颗豌豆造成的伤害
#define ZOMBIE_HP            100    // 普通僵尸总血量（100 / 20 = 5 发豌豆）
#define PEA_SPEED            3      // 豌豆每帧移动的像素数
#define PEA_SHOOT_INTERVAL   1400   // 豌豆射手开火间隔(毫秒)
#define SUNFLOWER_INTERVAL   8000   // 向日葵产阳光间隔(毫秒)
#define SUN_VALUE            25     // 每颗阳光的价值
#define ZOMBIE_DEATH_TIME    700    // 僵尸死后淡出消失的时间(毫秒)
#define CHOMPER_BITE_TIME    400    // 大嘴花咬合动画时长(毫秒)，GIF 加载失败时使用
#define CHOMPER_CHEW_TIME    8000   // 大嘴花冷却/咀嚼时长(毫秒)
#define CHOMPER_GIF_HEIGHT   88     // 大嘴花 GIF 缩放后的高度(像素)，和 PNG 一致
#define CHOMPER_GIF_OFFSET_X 0      // GIF 相对格子的横向微调
#define CHOMPER_GIF_OFFSET_Y 0      // GIF 相对格子的纵向微调
#define ZOMBIE_HITBOX        55     // 僵尸受击判定的半宽(像素)
#define SHOW_ZOMBIE_HP       0      // 1=显示僵尸血z条(调试用)，0=不显示

/* 卡牌下标：0=豌豆射手 1=向日葵 2=大嘴花 */
enum { wd, xrk, dzh, zwcount };

/* 三张卡牌的花费，和卡面上的数字一致 */
static const int plantCost[zwcount] = { 100, 50, 150 };

/* 草坪格子坐标：9 列 x 5 行 */
static const int colLeft[9] = { 253, 333, 411, 497, 576, 655, 739, 811, 896 };
static const int colRight[9] = { 333, 411, 497, 576, 655, 739, 811, 896, 986 };
static const int rowTop[5] = { 82, 180, 279, 383, 489 };
static const int rowBottom[5] = { 180, 279, 383, 488, 571 };
static const int colCenter[9] = { 293, 372, 454, 536, 615, 697, 775, 853, 941 };
static const int rowCenter[5] = { 131, 229, 331, 435, 529 };

/* ============================================================
 *                        全局资源
 * ============================================================ */
IMAGE imgbackground;
IMAGE imgstart, imgstartui1, imgstartui2;
IMAGE imgbelt;
IMAGE imgcards[zwcount];
IMAGE* imgzhiwu[zwcount][20];
int    plantFrameCount[zwcount] = { 0 };   // 每种植物实际加载了多少帧
IMAGE imgsunshineBall[29];
IMAGE imgzombie[22];
IMAGE imgbulletfly;
IMAGE imgbulletblast;

ExMessage msg;

int nowx, nowy;
int nowhandcard;
int sunshinenum = 0;
int dtime = 0;
int sunFlashTimer = 0;                         // 阳光不足时数字闪红的剩余时间
int timer[9] = { 0, 0, 3250, 0, 0, 0, 0, 0, 0 };
// timer2 阳光产生  timer3 下落速度  timer4 阳光动画
// timer5 僵尸刷新  timer6 僵尸移动  timer7 僵尸动画

struct shotTimer
{
	int  itime;    // 这个豌豆射手已经积攒的时间
	bool used;
} shootTimer[5][9];

struct PlantCell
{
	int type = 0;              // 0=空地，1..zwcount=卡牌序号
	int frameIndex = 0;        // 当前播放到第几帧
	int placex = -1, placey = -1;
	int animTimer = 0;         // 帧动画计时
	int produceTimer = 0;      // 向日葵产阳光计时
	int state = 0;             // 大嘴花状态：0待机 1攻击 2冷却(咀嚼)
	int stateTimer = 0;        // 大嘴花状态计时
	int biteDone = 0;          // 本次攻击是否已经吞掉僵尸
};
PlantCell map[5][9], nonemap;
int *nonex = &nonemap.placex, *noney = &nonemap.placey, nonerow, nonecolumn;

bool ifzombie[5];              // 每一行当前是否有活僵尸

struct sunshineBall
{
	int x, y;
	int frameIndex;
	int desty;
	bool used;
	int exsitTime;
} balls[20];

struct zombie
{
	int x, y;
	int frameIndex;
	bool used;
	int speed;
	int row;
	int hp;             // 当前血量
	int deathTimer;     // 死亡淡出计时
} zombies[20];

struct bullet
{
	int x, y;
	int row;
	bool used;
	int speed;
} bullets[50];

/* 豌豆命中时播放的小爆裂特效 */
struct hitEffect
{
	int x, y;
	int timer;
	bool used;
} effects[20];

/* ============================================================
 *                        工具函数
 * ============================================================ */
static bool fileExist(const char* name)
{
	FILE *fp;
	if (fopen_s(&fp, name, "r"))
	{
		return false;
	}
	else
	{
		fclose(fp);
		return true;
	}
}

static void playSound(const char* name)
{
	char cmd[128];
	sprintf_s(cmd, sizeof(cmd), "play res/audio/%s", name);
	mciSendString(cmd, 0, 0, 0);
}

/* ============================================================
 *                    GIF 动画加载（大嘴花）
 *   EasyX 不能直接播放 GIF，这里用 GDI+ 把每一帧解码成 IMAGE，
 *   并读取每一帧的延时，播放时按延时切换。
 * ============================================================ */
#define GIF_MAX_FRAMES 64

struct GifAnim
{
	IMAGE* frames[GIF_MAX_FRAMES];
	int    delays[GIF_MAX_FRAMES];   // 每帧显示时长(毫秒)
	int    count;
	int    total;
};

static GifAnim gifChomperAttack;
static GifAnim gifChomperDigest;

static bool loadGifFrames(const wchar_t* path, GifAnim& anim, int targetHeight)
{
	/* 只在本函数内启用 Gdiplus 命名空间，保证 PropertyItem 在
	   不同 SDK 版本下都能正确解析，又不影响外面的 EasyX 代码 */
	using namespace Gdiplus;

	anim.count = 0;
	anim.total = 0;

	Gdiplus::Image* image = Gdiplus::Image::FromFile(path);
	if (image == NULL) return false;
	if (image->GetLastStatus() != Gdiplus::Ok)
	{
		delete image;
		return false;
	}

	int srcW = (int)image->GetWidth();
	int srcH = (int)image->GetHeight();
	if (srcW <= 0 || srcH <= 0 || targetHeight <= 0)
	{
		delete image;
		return false;
	}

	int outH = targetHeight;
	int outW = srcW * outH / srcH;
	if (outW <= 0) outW = outH;

	UINT frameCount = image->GetFrameCount(&Gdiplus::FrameDimensionTime);
	if (frameCount == 0) frameCount = 1;
	if (frameCount > GIF_MAX_FRAMES) frameCount = GIF_MAX_FRAMES;

	/* 读取每帧延时，单位是 1/100 秒 */
	UINT propSize = image->GetPropertyItemSize(PropertyTagFrameDelay);
	PropertyItem* prop = NULL;
	LONG* delayValue = NULL;
	int delayCount = 0;
	if (propSize > 0)
	{
		prop = (PropertyItem*)malloc(propSize);
		if (prop != NULL &&
			image->GetPropertyItem(PropertyTagFrameDelay, propSize, prop) == Gdiplus::Ok)
		{
			delayValue = (LONG*)prop->value;
			delayCount = (int)(prop->length / sizeof(LONG));
		}
	}

	for (UINT i = 0; i < frameCount; i++)
	{
		if (image->SelectActiveFrame(&Gdiplus::FrameDimensionTime, i) != Gdiplus::Ok)
		{
			break;
		}

		/* 把当前帧缩放画到一张 32 位 ARGB 位图上 */
		Gdiplus::Bitmap out(outW, outH, PixelFormat32bppARGB);
		Gdiplus::Graphics g(&out);
		g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		g.DrawImage(image, Gdiplus::Rect(0, 0, outW, outH));

		IMAGE* frame = new IMAGE(outW, outH);
		DWORD* dst = GetImageBuffer(frame);

		Gdiplus::BitmapData data;
		Gdiplus::Rect rect(0, 0, outW, outH);
		if (out.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
		{
			delete frame;
			break;
		}

		BYTE* scan0 = (BYTE*)data.Scan0;
		int stride = data.Stride;
		if (stride >= 0)
		{
			for (int y = 0; y < outH; y++)
			{
				memcpy(dst + y * outW, scan0 + y * stride, outW * 4);
			}
		}
		else
		{
			stride = -stride;
			for (int y = 0; y < outH; y++)
			{
				memcpy(dst + y * outW, scan0 + (outH - 1 - y) * stride, outW * 4);
			}
		}
		out.UnlockBits(&data);

		int delay = 100;
		if (delayValue != NULL && (int)i < delayCount)
		{
			delay = (int)delayValue[i] * 10;   // 1/100 秒 -> 毫秒
			if (delay <= 0) delay = 100;       // 有些 GIF 延时写 0，给个默认值
		}

		anim.frames[anim.count] = frame;
		anim.delays[anim.count] = delay;
		anim.total += delay;
		anim.count++;
	}

	if (prop != NULL) free(prop);
	delete image;

	if (anim.count == 0)
	{
		anim.total = 0;
		return false;
	}
	return true;
}

/* 根据已经播放的时间，算出当前该显示第几帧 */
static int gifFrameAt(const GifAnim& anim, int elapsed)
{
	if (anim.count <= 0) return -1;
	if (anim.total <= 0) return 0;

	int t = elapsed % anim.total;
	int acc = 0;
	for (int i = 0; i < anim.count; i++)
	{
		acc += anim.delays[i];
		if (t < acc) return i;
	}
	return anim.count - 1;
}

/* ============================================================
 *                        开始界面
 * ============================================================ */
static void startUI()
{
	mciSendString("play res/bg.mp3", 0, 0, 0);
	initgraph(900, WIN_HEIGHT, 1);
	loadimage(&imgstart, "res/startbg.jpg");
	loadimage(&imgstartui1, "res/menu1.png");
	loadimage(&imgstartui2, "res/menu2.png");
	putimage(0, 0, &imgstart);
	bool uistatu = false;
	while (1)
	{
		if (peekmessage(&msg))
		{
			if (msg.message == WM_LBUTTONDOWN)
			{
				if (msg.x >= 478 && msg.y >= 89 && msg.x <= 478 + 331 && msg.y <= 89 + 147)
				{
					uistatu = true;
				}
			}
		}
		if (uistatu)
		{
			mciSendString("play res/audio/gravebutton.mp3", 0, 0, 0);
			putimagePNG(478, 89, &imgstartui2);
			mciSendString("close res/bg.mp3", 0, 0, 0);
			mciSendString("play res/audio/evillaugh.mp3", 0, 0, 0);
			Sleep(3500);
			sunshinenum = 50;
			break;
		}
		else
		{
			putimagePNG(478, 89, &imgstartui1);
		}
	}
}

/* ============================================================
 *                鼠标坐标 -> 行列 / 行列 -> 坐标
 * ============================================================ */
static void rowcolumn(int &x, int &y, int *xx, int *yy)
{
	x = -1;
	y = -1;
	for (int r = 0; r < 5; r++)
	{
		if (msg.y >= rowTop[r] && msg.y < rowBottom[r])
		{
			y = r;
			*yy = rowCenter[r];
		}
	}
	for (int c = 0; c < 9; c++)
	{
		if (msg.x >= colLeft[c] && msg.x < colRight[c])
		{
			x = c;
			*xx = colCenter[c];
		}
	}
}

static void place(int row, int column, int &a, int &b)
{
	a = colCenter[column];
	b = rowCenter[row];
}

/* ============================================================
 *                        阳光系统
 * ============================================================ */

/* 在指定位置放一颗阳光；y 是出生点，desty 是落到哪一行停下 */
static bool spawnSun(int x, int y, int desty)
{
	for (int i = 0; i < 20; i++)
	{
		if (balls[i].used == false)
		{
			balls[i].used = true;
			balls[i].frameIndex = 0;
			balls[i].x = x;
			balls[i].y = y;
			balls[i].desty = desty;
			balls[i].exsitTime = 0;
			return true;
		}
	}
	return false;
}

/* 天上掉下来的阳光 */
static void createSunshine()
{
	spawnSun(290 + rand() % 670, 130, 130 + rand() % 420);
}

/* 向日葵产的阳光：从花头顶冒出来，落到花旁边。
   第一行的花不能把阳光生成到顶部卡片栏上，所以把 y 限制在 130 以下 */
static void createSunshineFromPlant(int x, int y)
{
	int fromY = y - 55;
	int toY = y - 20;
	if (fromY < 130) fromY = 130;
	if (toY < 130) toY = 130;
	spawnSun(x + rand() % 31 - 15, fromY, toY);
}

static void updateSunshine()
{
	for (int i = 0; i < 20; i++)
	{
		if (balls[i].used)
		{
			if (timer[3] >= 15)
			{
				balls[i].y += 1;
			}
			if (timer[4] >= 40)
			{
				balls[i].frameIndex = (balls[i].frameIndex + 1) % 29;
			}
			if (balls[i].y >= balls[i].desty)
			{
				balls[i].y = balls[i].desty;
			}
		}
	}
	if (timer[3] >= 15)
	{
		timer[3] -= 15;
	}
	if (timer[4] >= 40)
	{
		timer[4] -= 40;
	}
}

/* ============================================================
 *                        僵尸系统
 * ============================================================ */
static void createZombie()
{
	static int createdzm = 0;
	static int zmFre = 10000;
	static bool firstzm = true;
	if (timer[5] >= zmFre)
	{
		timer[5] = 0;
		zmFre = 28000 + (rand() % 7000);
		static int zmcreaten = 1 + rand() % 2;
		if (firstzm) firstzm = false;
		else zmcreaten = 2 + (rand() % 3);
		for (int j = 0; j < zmcreaten; j++)
		{
			for (int i = 0; i < 20; i++)
			{
				if (zombies[i].used == false)
				{
					createdzm += 1;
					printf("zombie created: %d\n", createdzm);
					zombies[i].used = true;
					zombies[i].frameIndex = 0;
					zombies[i].row = (rand() % 5);
					zombies[i].y = rowCenter[zombies[i].row] - 30;
					zombies[i].x = WIN_WIDTH;
					zombies[i].speed = 1;
					zombies[i].hp = ZOMBIE_HP;       // 出生时满血
					zombies[i].deathTimer = 0;
					break;
				}
			}
		}
	}
}

/* 扣血；血量归零后进入死亡淡出状态 */
static void damageZombie(int index, int damage)
{
	if (zombies[index].hp <= 0) return;
	zombies[index].hp -= damage;
	if (zombies[index].hp <= 0)
	{
		zombies[index].hp = 0;
		zombies[index].deathTimer = 0;
		playSound("groan1.mp3");
	}
	else
	{
		playSound("splat201.mp3");
	}
}

/* 被大嘴花吞掉：直接移除，不播放倒地动画 */
static void swallowZombie(int index)
{
	zombies[index].used = false;
	zombies[index].hp = 0;
	zombies[index].deathTimer = 0;
}

static void updatezombie()
{
	if (timer[6] >= 65)
	{
		for (int i = 0; i < 20; i++)
		{
			if (zombies[i].used && zombies[i].hp > 0)
			{
				zombies[i].x = zombies[i].x - zombies[i].speed;
				if (zombies[i].x <= 210)
				{
					printf("Game Lose!\n");
				}
			}
		}
		timer[6] -= 65;
	}
	if (timer[7] >= 70)
	{
		for (int i = 0; i < 20; i++)
		{
			if (zombies[i].used && zombies[i].hp > 0)
			{
				zombies[i].frameIndex = (zombies[i].frameIndex + 1) % 22;
			}
		}
		timer[7] -= 70;
	}
	/* 死亡计时：停止移动，淡出后回收 */
	for (int i = 0; i < 20; i++)
	{
		if (zombies[i].used && zombies[i].hp <= 0)
		{
			zombies[i].deathTimer += dtime;
			if (zombies[i].deathTimer >= ZOMBIE_DEATH_TIME)
			{
				zombies[i].used = false;
				zombies[i].deathTimer = 0;
			}
		}
	}
}

/* ============================================================
 *                        阳光收集
 * ============================================================ */
static bool collectSunshine(ExMessage *msg)
{
	bool collected = false;
	for (int i = 0; i < 20; i++)
	{
		if (balls[i].used == true)
		{
			int x1 = balls[i].x - 40;
			int x2 = balls[i].x + 40;
			int y1 = balls[i].y - 40;
			int y2 = balls[i].y + 40;
			if (msg->x >= x1 && msg->x <= x2 && msg->y >= y1 && msg->y <= y2)
			{
				static int n = 0;
				if (n == 0)
				{
					playSound("points.mp3");
					n++;
				}
				else if (n == 1)
				{
					playSound("points02.mp3");
					n++;
				}
				else
				{
					playSound("points03.mp3");
					n -= 2;
				}
				balls[i].used = false;
				sunshinenum += SUN_VALUE;
				collected = true;
			}
		}
	}
	return collected;
}

/* ============================================================
 *                        豌豆射手
 * ============================================================ */

/* dt：本帧经过的毫秒数。每个豌豆射手用自己的 shootTimer，互不干扰 */
static void shoot(int dt)
{
	for (int i = 0; i < 5; i++)
	{
		for (int j = 0; j < 9; j++)
		{
			if (map[i][j].type != wd + 1)
			{
				shootTimer[i][j].used = false;
				shootTimer[i][j].itime = 0;
				continue;
			}
			shootTimer[i][j].used = true;
			shootTimer[i][j].itime += dt;

			if (ifzombie[i] == false) continue;
			if (shootTimer[i][j].itime < PEA_SHOOT_INTERVAL) continue;

			for (int k = 0; k < 50; k++)
			{
				if (bullets[k].used == false)
				{
					bullets[k].used = true;
					bullets[k].speed = PEA_SPEED;
					bullets[k].row = i;
					place(i, j, bullets[k].x, bullets[k].y);
					bullets[k].x += 15;
					bullets[k].y -= 24 + rand() % 3;
					shootTimer[i][j].itime = 0;
					break;
				}
			}
			/* 没有空闲子弹时不要无限攒时间，否则会连发 */
			if (shootTimer[i][j].itime > PEA_SHOOT_INTERVAL)
			{
				shootTimer[i][j].itime = PEA_SHOOT_INTERVAL;
			}
		}
	}
}

/* ============================================================
 *                        子弹与命中
 * ============================================================ */
static void addHitEffect(int x, int y)
{
	for (int i = 0; i < 20; i++)
	{
		if (effects[i].used == false)
		{
			effects[i].used = true;
			effects[i].x = x;
			effects[i].y = y;
			effects[i].timer = 140;
			return;
		}
	}
}

static void updateEffects(int dt)
{
	for (int i = 0; i < 20; i++)
	{
		if (effects[i].used)
		{
			effects[i].timer -= dt;
			if (effects[i].timer <= 0)
			{
				effects[i].used = false;
			}
		}
	}
}

static void updatebullets()
{
	for (int i = 0; i < 50; i++)
	{
		if (bullets[i].used == false) continue;

		bullets[i].x += bullets[i].speed;

		/* 用豌豆中心去和同一行僵尸做横向碰撞检测 */
		int bulletCenter = bullets[i].x + 10;
		int hitIndex = -1;
		for (int k = 0; k < 20; k++)
		{
			if (zombies[k].used && zombies[k].hp > 0 &&
				zombies[k].row == bullets[i].row)
			{
				if (bulletCenter >= zombies[k].x - ZOMBIE_HITBOX &&
					bulletCenter <= zombies[k].x + ZOMBIE_HITBOX)
				{
					/* 同一帧有多只重叠时，优先打最靠前的那只 */
					if (hitIndex < 0 || zombies[k].x < zombies[hitIndex].x)
					{
						hitIndex = k;
					}
				}
			}
		}

		if (hitIndex >= 0)
		{
			int hx = bullets[i].x + 10;
			int hy = bullets[i].y + 10;
			bullets[i].used = false;
			damageZombie(hitIndex, PEA_DAMAGE);
			addHitEffect(hx, hy);
		}
		else if (bullets[i].x >= WIN_WIDTH)
		{
			bullets[i].used = false;
		}
	}
}

/* ============================================================
 *                        大嘴花
 * ============================================================ */
static int findZombieInFront(int row, int plantX)
{
	int target = -1;
	for (int k = 0; k < 20; k++)
	{
		if (zombies[k].used && zombies[k].hp > 0 && zombies[k].row == row &&
			zombies[k].x >= plantX - 10 &&
			zombies[k].x <= plantX + 85)
		{
			if (target < 0 || zombies[k].x < zombies[target].x)
			{
				target = k;
			}
		}
	}
	return target;
}

static int chomperAttackDuration()
{
	if (gifChomperAttack.count > 0 && gifChomperAttack.total > 0)
	{
		return gifChomperAttack.total;
	}
	return CHOMPER_BITE_TIME;
}

static void updateChomper(PlantCell &cell, int row, int dt)
{
	if (cell.state == 0)
	{
		/* 待机：继续用原来的 PNG 帧循环 */
		if (cell.animTimer >= 220)
		{
			cell.animTimer -= 220;
			cell.frameIndex = (cell.frameIndex + 1) % 3;
		}

		int target = findZombieInFront(row, cell.placex);
		if (target >= 0)
		{
			cell.state = 1;          // 进入攻击
			cell.stateTimer = 0;
			cell.biteDone = 0;
			cell.frameIndex = 0;
		}
	}
	else if (cell.state == 1)
	{
		/* 攻击：播放 ChomperAttack.gif，播完进入冷却 */
		cell.stateTimer += dt;
		int total = chomperAttackDuration();

		/* 播放到一半时真正吞掉僵尸，动作和判定对得上 */
		if (cell.biteDone == 0 && cell.stateTimer >= total / 2)
		{
			int target = findZombieInFront(row, cell.placex);
			if (target >= 0)
			{
				swallowZombie(target);
				playSound("chomp.mp3");
			}
			cell.biteDone = 1;
		}

		/* GIF 没加载成功时，退回 PNG 咬合帧 */
		if (gifChomperAttack.count == 0)
		{
			int f = cell.stateTimer / 100;
			if (f > 3) f = 3;
			cell.frameIndex = f;
		}

		if (cell.stateTimer >= total)
		{
			cell.state = 2;          // 进入冷却
			cell.stateTimer = 0;
			cell.frameIndex = 3;
		}
	}
	else
	{
		/* 冷却：循环播放 ChomperDigest.gif，期间不能再攻击 */
		cell.stateTimer += dt;
		if (cell.stateTimer >= CHOMPER_CHEW_TIME)
		{
			cell.state = 0;
			cell.stateTimer = 0;
			cell.frameIndex = 0;
			cell.animTimer = 0;
		}
		else if (gifChomperDigest.count == 0)
		{
			/* GIF 没加载成功时，退回 PNG 咀嚼帧 */
			cell.frameIndex = 2 + (cell.stateTimer / 400) % 2;
		}
	}
}

/* ============================================================
 *                        植物更新
 * ============================================================ */
static void updatePlants(int dt)
{
	for (int i = 0; i < 5; i++)
	{
		for (int j = 0; j < 9; j++)
		{
			PlantCell &cell = map[i][j];
			if (cell.type == 0) continue;

			int type = cell.type - 1;
			if (type < 0 || type >= zwcount) continue;
			if (plantFrameCount[type] <= 0) continue;

			cell.animTimer += dt;

			if (type == xrk)
			{
				/* 向日葵：定时产阳光 */
				cell.produceTimer += dt;
				if (cell.produceTimer >= SUNFLOWER_INTERVAL)
				{
					cell.produceTimer -= SUNFLOWER_INTERVAL;
					createSunshineFromPlant(cell.placex, cell.placey);
				}

				if (cell.animTimer >= 120)
				{
					cell.animTimer -= 120;
					cell.frameIndex = (cell.frameIndex + 1) % plantFrameCount[type];
				}
			}
			else if (type == dzh)
			{
				updateChomper(cell, i, dt);
			}
			else
			{
				/* 豌豆射手：普通循环动画 */
				if (cell.animTimer >= 100)
				{
					cell.animTimer -= 100;
					cell.frameIndex = (cell.frameIndex + 1) % plantFrameCount[type];
				}
			}
		}
	}
}

/* ============================================================
 *                        游戏初始化
 * ============================================================ */
static void gameinit()
{
	playSound("readysetplant.mp3");
	loadimage(&imgbackground, "res/Map/map0.jpg");
	loadimage(&imgbelt, "res/belt4.png");
	loadimage(&imgbulletfly, "res/bullets/bullet_normal.png");
	loadimage(&imgbulletblast, "res/bullets/bullet_blast.png");

	memset(imgzhiwu, 0, sizeof(imgzhiwu));
	memset(map, 0, sizeof(map));
	memset(balls, 0, sizeof(balls));
	memset(zombies, 0, sizeof(zombies));
	memset(bullets, 0, sizeof(bullets));
	memset(effects, 0, sizeof(effects));
	memset(shootTimer, 0, sizeof(shootTimer));
	memset(ifzombie, 0, sizeof(ifzombie));

	char name[64];
	for (int i = 0; i < zwcount; i++)
	{
		sprintf_s(name, sizeof(name), "res/Cards/card_%d.png", i + 1);
		loadimage(&imgcards[i], name, 64, 82);

		plantFrameCount[i] = 0;
		for (int j = 0; j < 20; j++)
		{
			sprintf_s(name, sizeof(name), "res/zhiwu/%d/%d.png", i, j + 1);
			if (fileExist(name))
			{
				imgzhiwu[i][j] = new IMAGE;
				if (i == 2)
				{
					loadimage(imgzhiwu[i][j], name, 80, 88);
				}
				else
				{
					loadimage(imgzhiwu[i][j], name);
				}
				plantFrameCount[i] = j + 1;
			}
			else
			{
				break;
			}
		}
	}
	nowhandcard = 0;
	sunFlashTimer = 0;

	for (int i = 0; i < 29; i++)
	{
		sprintf_s(name, sizeof(name), "res/sunshine/%d.png", i + 1);
		loadimage(&imgsunshineBall[i], name);
	}

	for (int i = 0; i < 22; i++)
	{
		sprintf_s(name, sizeof(name), "res/zm/%d.png", i + 1);
		loadimage(&imgzombie[i], name);
	}

	initgraph(WIN_WIDTH, WIN_HEIGHT, 1);

	/* 大嘴花的 GIF 动画：攻击 / 冷却咀嚼 */
	loadGifFrames(L"res/Plants/Chomper/ChomperAttack.gif", gifChomperAttack, CHOMPER_GIF_HEIGHT);
	loadGifFrames(L"res/Plants/Chomper/ChomperDigest.gif", gifChomperDigest, CHOMPER_GIF_HEIGHT);
}

/* ============================================================
 *                带整体透明度绘制（死亡淡出用）
 * ============================================================ */
static void putimagePNGWithAlpha(int x, int y, IMAGE* picture, int alpha)
{
	if (alpha >= 255)
	{
		putimagePNG(x, y, picture);
		return;
	}
	if (alpha <= 0) return;

	int w = picture->getwidth();
	int h = picture->getheight();

	IMAGE tmp;
	SetWorkingImage(picture);
	getimage(&tmp, 0, 0, w, h);
	SetWorkingImage();

	DWORD* buf = GetImageBuffer(&tmp);
	int count = w * h;
	for (int i = 0; i < count; i++)
	{
		DWORD a = (buf[i] >> 24) & 0xff;
		buf[i] = (buf[i] & 0x00ffffff) | ((a * alpha / 255) << 24);
	}

	putimagePNG(x, y, &tmp);
}
/* ============================================================
 *                        画面更新
 * ============================================================ */
static void updatewindow()
{
	BeginBatchDraw();
	putimage(0, 0, &imgbackground);
	putimage(230, 0, &imgbelt);

	for (int i = 0; i < zwcount; i++)
	{
		putimage(319 + 73 * i, 8, &imgcards[i]);
	}

	/* 手上拿着的植物跟着鼠标 */
	if (nowhandcard)
	{
		IMAGE *img = imgzhiwu[nowhandcard - 1][0];
		if (img != NULL)
		{
			putimagePNG(nowx - img->getwidth() / 2, nowy - img->getheight() / 2, img);
		}
	}

	/* 植物 */
	for (int i = 0; i < 5; i++)
	{
		for (int j = 0; j < 9; j++)
		{
			if (map[i][j].type)
			{
				int x = map[i][j].placex;
				int y = map[i][j].placey;
				int type = map[i][j].type - 1;
				if (type < 0 || type >= zwcount) continue;

				if (type == 2)
				{
					x += 8;
					y -= 5;

					/* 大嘴花：攻击 / 冷却阶段优先使用 GIF 帧 */
					IMAGE* gifImg = NULL;
					if (map[i][j].state == 1 && gifChomperAttack.count > 0)
					{
						int fi = gifFrameAt(gifChomperAttack, map[i][j].stateTimer);
						if (fi >= 0) gifImg = gifChomperAttack.frames[fi];
					}
					else if (map[i][j].state == 2 && gifChomperDigest.count > 0)
					{
						int fi = gifFrameAt(gifChomperDigest, map[i][j].stateTimer);
						if (fi >= 0) gifImg = gifChomperDigest.frames[fi];
					}

					if (gifImg != NULL)
					{
						putimagePNG(x + CHOMPER_GIF_OFFSET_X - gifImg->getwidth() / 2,
							y + CHOMPER_GIF_OFFSET_Y - gifImg->getheight() / 2, gifImg);
						continue;
					}
				}

				int index = map[i][j].frameIndex;
				if (index < 0 || index >= plantFrameCount[type]) index = 0;
				IMAGE* img2 = imgzhiwu[type][index];
				if (img2 == NULL) continue;
				putimagePNG(x - img2->getwidth() / 2, y - img2->getheight() / 2, img2);
			}
		}
	}

	/* 天上定时掉落的阳光 */
	if (timer[2] >= 9500)
	{
		createSunshine();
		timer[2] -= 9500;
	}
	updateSunshine();

	createZombie();
	updatezombie();

	/* 阳光 */
	for (int i = 0; i < 20; i++)
	{
		if (balls[i].used)
		{
			putimagePNG(balls[i].x - 40, balls[i].y - 40, &imgsunshineBall[balls[i].frameIndex]);
		}
	}

	/* 僵尸 */
	for (int i = 0; i < 20; i++)
	{
		if (zombies[i].used == false) continue;

		IMAGE *img3 = &imgzombie[zombies[i].frameIndex];
		int x = zombies[i].x - img3->getwidth() / 2;
		int y = zombies[i].y - img3->getheight() / 2;

		if (zombies[i].hp <= 0)
		{
			/* 死亡：向下沉并逐渐变透明 */
			int t = zombies[i].deathTimer;
			if (t > ZOMBIE_DEATH_TIME) t = ZOMBIE_DEATH_TIME;
			int alpha = 255 - t * 255 / ZOMBIE_DEATH_TIME;
			int offsetY = t * 24 / ZOMBIE_DEATH_TIME;
			putimagePNGWithAlpha(x, y + offsetY, img3, alpha);
		}
		else
		{
			putimagePNG(x, y, img3);
		}

#if SHOW_ZOMBIE_HP
		if (zombies[i].hp > 0)
		{
			int barW = 44;
			int bx = zombies[i].x - barW / 2;
			int by = y - 10;
			setfillcolor(RGB(50, 50, 50));
			solidrectangle(bx - 1, by - 1, bx + barW + 1, by + 6);
			setfillcolor(RGB(90, 210, 60));
			solidrectangle(bx, by, bx + barW * zombies[i].hp / ZOMBIE_HP, by + 5);
		}
#endif
	}

	/* 豌豆命中特效 */
	for (int i = 0; i < 20; i++)
	{
		if (effects[i].used)
		{
			int w = imgbulletblast.getwidth();
			int h = imgbulletblast.getheight();
			putimagePNG(effects[i].x - w / 2, effects[i].y - h / 2, &imgbulletblast);
		}
	}

	/* 豌豆 */
	updatebullets();
	for (int i = 0; i < 50; i++)
	{
		if (bullets[i].used)
		{
			putimagePNG(bullets[i].x, bullets[i].y, &imgbulletfly);
		}
	}

	/* 阳光数量 */
	LOGFONT f;
	char sunshinetext[8];
	sprintf_s(sunshinetext, sizeof(sunshinetext), "%d", sunshinenum);
	gettextstyle(&f);
	f.lfHeight = 27;
	f.lfWeight = 16;
	strcpy_s(f.lfFaceName, 20, "Times New Roman");
	f.lfQuality = ANTIALIASED_QUALITY;
	settextstyle(&f);
	setbkmode(TRANSPARENT);
	setcolor(sunFlashTimer > 0 ? RGB(220, 30, 30) : BLACK);
	outtextxy(261, 69, sunshinetext);

	EndBatchDraw();
}

/* ============================================================
 *                        鼠标交互
 * ============================================================ */
static void userclick()
{
	if (peekmessage(&msg) == false) return;

	if (msg.message == WM_MOUSEMOVE && nowhandcard != 0)
	{
		nowx = msg.x;
		nowy = msg.y;
		return;
	}

	if (msg.message == WM_RBUTTONDOWN)
	{
		nowhandcard = 0;
		playSound("tap.mp3");
		return;
	}

	if (msg.message != WM_LBUTTONDOWN) return;

	/* 1. 是否点中了卡片栏 */
	bool clickedBelt = false;
	if (msg.x >= 319 && msg.x <= 319 + 73 * zwcount && msg.y >= 8 && msg.y <= 90)
	{
		for (int i = 0; i < zwcount; i++)
		{
			if (msg.x >= 319 + 73 * i && msg.x <= 319 + 73 * i + 64 &&
				msg.y >= 8 && msg.y <= 90)
			{
				clickedBelt = true;
				int index = i + 1;
				if (nowhandcard != index)
				{
					playSound("seedlift.mp3");
					nowhandcard = index;
					nowx = msg.x;
					nowy = msg.y;
					if (sunshinenum < plantCost[i])
					{
						sunFlashTimer = 800;      // 钱不够，先闪一下红光
					}
				}
				else
				{
					playSound("tap.mp3");
					nowhandcard = 0;
				}
				break;
			}
		}
	}

	if (clickedBelt) return;

	/* 顶部 90 像素是卡片栏/阳光数 UI，不允许在这里种植 */
	if (msg.y < 90) return;

	/* 2. 点空地时先尝试收阳光；
	   如果这一下点中了阳光，就只收阳光，不要顺手把植物种下去 */
	if (collectSunshine(&msg)) return;

	if (nowhandcard == 0) return;

	/* 3. 尝试种植 */
	int row = -1, column = -1;
	rowcolumn(column, row, nonex, noney);
	if (row < 0 || column < 0) return;

	if (map[row][column].type != 0)
	{
		playSound("tap.mp3");             // 这个格子已经有植物
		return;
	}

	int cost = plantCost[nowhandcard - 1];
	if (sunshinenum < cost)
	{
		sunFlashTimer = 800;              // 阳光不足，数字闪红
		return;
	}

	/* 4. 种下植物并扣阳光 */
	playSound("plant1.mp3");
	map[row][column].type = nowhandcard;
	map[row][column].frameIndex = 0;
	map[row][column].placex = colCenter[column];
	map[row][column].placey = rowCenter[row];
	map[row][column].animTimer = 0;
	map[row][column].produceTimer = 0;
	map[row][column].state = 0;
	map[row][column].stateTimer = 0;
	map[row][column].biteDone = 0;
	sunshinenum -= cost;
	nowhandcard = 0;
}

/* ============================================================
 *                每 80ms 更新一次的行状态
 * ============================================================ */
static void updateGame()
{
	for (int i = 0; i < 5; i++)
	{
		ifzombie[i] = false;
	}
	for (int i = 0; i < 20; i++)
	{
		if (zombies[i].used && zombies[i].hp > 0)
		{
			if (zombies[i].row >= 0 && zombies[i].row < 5)
			{
				ifzombie[zombies[i].row] = true;
			}
		}
	}
}

/* ============================================================
 *                        主循环
 * ============================================================ */
int main(void)
{
	/* 初始化 GDI+，用于把大嘴花的 GIF 解码成 IMAGE 帧 */
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken = 0;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

	srand((unsigned int)time(NULL));
	startUI();
	gameinit();

	while (1)
	{
		dtime = getDelay();

		timer[1] += dtime;
		timer[2] += dtime;
		timer[3] += dtime;
		timer[4] += dtime;
		timer[5] += dtime;
		timer[6] += dtime;
		timer[7] += dtime;
		timer[8] += dtime;

		if (sunFlashTimer > 0)
		{
			sunFlashTimer -= dtime;
			if (sunFlashTimer < 0) sunFlashTimer = 0;
		}

		/* 进入游戏后延迟一会儿开始播放背景音乐 */
		static bool ifbgm = false;
		if (ifbgm == false && timer[2] >= 5000)
		{
			mciSendString("play res/audio/grasswalk.mp3", 0, 0, 0);
			ifbgm = true;
		}

		updatePlants(dtime);
		updateEffects(dtime);
		shoot(dtime);

		userclick();

		if (timer[1] >= 15)
		{
			updatewindow();
			if (timer[1] >= 80)
			{
				updateGame();
				timer[1] -= 80;
			}
		}
	}

	return 0;
}
