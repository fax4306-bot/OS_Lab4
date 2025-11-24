#include <pmm.h>
#include <list.h>
#include <string.h>
#include <slub_pmm.h>
#include <stdio.h>

//-------------定义slub机制需要的结构体---------------
//使用typedef可以在声明结构体变量时直接使用别名
//kmem_cache表示一个缓存池,专门管理固定大小的对象。
typedef struct cache{
    list_entry_t slab;//双向slab链表的起始节点
    size_t object_size;//该缓冲池对应的对象大小
    size_t object_num;//该缓冲池对应的一个slab中的对象数量
}kmem_cache;

//一页内存的管理结构，负责管理多个同样大小的对象
typedef struct SLAB{
    list_entry_t slab_node;//用于插入slab链表
    size_t free_cnt;//该slab中空闲object的数量
    void *obj_area;//该slab中存放对象的起始地址
    unsigned char *bitmap;//指向 slab 内存中存放位图的起始地址，位图用于标识对象是否被占用
}slab_t;

//管理所有缓冲池
static kmem_cache kmalloc_caches[4];
static size_t cache_n=0;

//-------------对两层分配进行初始化-----------
/*
该部分函数调用关系
slub_init->defult_init
         ->kmem_cache_init->cal_objs_num
*/
//第一层按页分配采用default方法
static free_area_t free_area;
#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)
static void
default_init(void) {
    list_init(&free_list);
    nr_free = 0;
}
//第二层缓存池初始化：初始化每个缓冲池的对象大小、对象数量、对应slab链表

//计算slab中能存放的对象数量
static size_t cal_objs_num(size_t object_size){
    //PGSIZE在mmu.h中定义为4096
    //对象可用的总空间=slab大小(1页)-slab_t描述信息
    //每个对象需要的空间=自身大小+所需的1bit位图
    //对象数量=对象可用的总空间/每个对象需要的空间
    size_t objects_num=(PGSIZE-sizeof(slab_t))/(object_size+1.0/8.0);
    if(objects_num==0){
        objects_num=1;
    }
    return objects_num;
}
//缓存池初始化函数
static void kmem_cache_init(void){
    cache_n=4;
    size_t sizes[4]={32,64,128,256};
    for(int i=0;i<cache_n;i++)
    {
        kmalloc_caches[i].object_size=sizes[i];
        kmalloc_caches[i].object_num=cal_objs_num(sizes[i]);
        //list_init函数位于list.h 用于初始化一个空链表
        list_init(&kmalloc_caches[i].slab);
    }
}
//整个slub系统的初始化函数：依次调取一二层初始化函数
static void slub_init(void){
    default_init();
    kmem_cache_init();
}

//--------------页级分配：将空闲物理页加入页级分配的链表(采用f-fit的default方法)----------
/*
default_init_memmap 的功能是：
将一段连续的物理页（从 base 开始，长度 n 页）初始化为可用（free）的页块，
并把这块连续的空闲页按地址顺序插入到全局空闲页链表 free_list 中，
同时更新空闲页计数 nr_free 与每页的元信息（flags、property、引用计数等）。
*/
static void
default_init_memmap(struct Page *base, size_t n) {
    //断言传入的页数 n 必须 > 0；否则会终止
    assert(n > 0);
    //用局部指针 p 遍历这段 Page 数组（每个 Page 代表一页物理内存的元信息结构）
    struct Page *p = base;
    //遍历 base 到 base + n - 1 的每个 Page。
    for (; p != base + n; p ++) {
        //断言每个 Page 在此之前是被标记为 reserved。
        assert(PageReserved(p));
        //将该页的 flags（状态位，与标识为reserved相关）清零，并把 property 清 0。property 字段通常在空闲块的首个页中保存连续空闲页数；对非首页置 0。
        p->flags = p->property = 0;
        //将该页的引用计数（ref）置为 0，表示此时没人引用该物理页。
        set_page_ref(p, 0);
    }
    //在这段连续空闲块的第一个 Page（head）上写入 property = n，表示从这里开始连续有 n 页空闲。
    base->property = n;
    //将 base 标记为“具有 property 属性”的页，通常通过设置某个位（宏）来标记该页是一个空闲块的头（便于后续合并/分配）。
    SetPageProperty(base);
    //更新全局空闲页计数，把 n 页加入空闲总数。
    nr_free += n;
    //如果全局空闲链表是空的，直接把 base 的 page_link 插入（通常插入到链表头后）。
    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    } else {
        //le 指向链表头（free_list 是链表头的 list_entry_t）
        list_entry_t* le = &free_list;
        //顺序遍历链表的每个节点（list_next 返回下一个节点）。
        while ((le = list_next(le)) != &free_list) {
            //把链表节点 le（即某个 Page 的 page_link）映射回包含它的 struct Page *（le2page 是常见的宏，依据偏移计算得到包含结构体）。
            struct Page* page = le2page(le, page_link);
            //如果要插入的 base 地址小于当前遍历到的 page 地址，则把 base 插在当前节点之前并 break（完成插入）。
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                //如果当前节点是链表的最后一个元素（即它的 next 指向头 &free_list），则把 base 插在它之后（即链表尾部）。
                list_add(le, &(base->page_link));
            }
        }
    }
}
static void
slub_init_memmap(struct Page *base,size_t n){
    default_init_memmap(base,n);
}
//----------------页级分配：使用第一层分配器处理大于页的内存需求(采用default方法)---------
//用于从空闲页链表中分配连续的 n 页
static struct Page *
default_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }
    if (page != NULL) {
        list_entry_t* prev = list_prev(&(page->page_link));
        list_del(&(page->page_link));
        if (page->property > n) {
            struct Page *p = page + n;
            p->property = page->property - n;
            SetPageProperty(p);
            list_add(prev, &(p->page_link));
        }
        nr_free -= n;
        ClearPageProperty(page);
    }
    return page;
}

//------------------分配一个新的slab---------------
static slab_t* alloc_slab(size_t object_size,size_t object_num)
{
    //为新的slub分配一页
    struct Page* p=default_alloc_pages(1);
    if(p==NULL)
    {
        return NULL;
    }
    //由页的结构体指针转为该物理页的虚拟地址
    //调用关系为KADDR(将物理地址转换为虚拟地址)->pagepa(将页号转为物理地址)->page2pnn(将page结构体指针转为页号)
    void *va=KADDR(page2pa(p));
    //将新分配的slab指针指向给它分配的页的虚拟地址
    slab_t *slab=(slab_t*)va;
    slab->free_cnt=object_num;
    //一个slab的布局：slab_struct|object|bitmap
    slab->obj_area=(void*)slab+sizeof(slab_t);
    slab->bitmap=(unsigned char*)((void*)slab->obj_area+object_size*object_num);
    //将位图全部初始化为 0,其中(objs_num + 7) / 8 → 向上取整，保证足够字节存放所有对象位
    memset(slab->bitmap, 0, (object_num + 7) / 8);
    //初始化slab链表节点
    list_init(&slab->slab_node);
    //返回 slab 的虚拟地址指针
    return slab;
}
//-----------------------二层分配：从缓冲池中分配合适的object-----------------//
//参数：请求的内存大小 返回值：指向分配的object的起始地址指针
//找合适object的步骤：kmalloc_caches中的kmem_cache->kmem_cache的slab链表->slab链表中的object,每次查找均采用firstfit
/*
typedef struct cache{
    list_entry_t slab;//双向slab链表的节点
    size_t object_size;//该缓冲池对应的对象大小
    size_t object_num;//该缓冲池对应的一个slab对象数量
}kmem_cache;
*/
// 将链表节点转换为对应的 SLAB 结构体
#define le2slab(le, member)                 \
    to_struct((le), struct SLAB, member)

static void* alloc_obj(size_t size)
{
    //请求内存大小不合法则返回NULL
    if(size<=0)return NULL;
    kmem_cache *fit_cache=NULL;
    for(int i=0;i<cache_n;i++)
    {
        if(kmalloc_caches[i].object_size>=size)
        {
            fit_cache=&kmalloc_caches[i];
            break;
        }
    }
    //请求内存大小大于所有缓存池中最大的对象大小时均返回NULL
    if(fit_cache==NULL)
    {
        return NULL;
    }
    //用指针le遍历对应cache的slab链表，找到第一个有空闲的slab并取下空闲的object
    list_entry_t *le=&fit_cache->slab;
    while((le=list_next(le))!=&fit_cache->slab)
    {
        //根据链表上的节点获取对应的结构体指针来帮助后续信息使用
        slab_t *fit_slab=le2slab(le,slab_node);
        if(fit_slab->free_cnt>0)
        {
            for(size_t i=0;i<fit_cache->object_num;i++)
            {
                //位图按字节存储
                //byte 是对象 i 所在的 位图字节下标（第几个字节）
                size_t byte=i/8;
                //bit 是对象 i 在该字节内对应的 位位置（0..7）
                size_t bit=i%8;
                //1 << bit 生成一个掩码，该掩码在第 bit 位为 1，其它位为 0，通过该掩码找到位图中一位的值。
                if((fit_slab->bitmap[byte]&(1<<bit))==0)
                {
                    //将object对应的位图中的位置1
                    fit_slab->bitmap[byte] |= (1 << bit);
                    //将对应slab的空闲对象计数减1
                    fit_slab->free_cnt--;
                    return (void *)((char *)fit_slab->obj_area + i * fit_cache->object_size);
                }
            }
        }
    }
    //若不存在空闲的slab->新建slab并分配第一个object
    slab_t *new_slab=alloc_slab(fit_cache->object_size,fit_cache->object_num);
    if(!new_slab)return NULL;
    //新的slab节点插入缓存池的slab链表
    list_add(&fit_cache->slab,&new_slab->slab_node);
    new_slab->bitmap[0]|=1;
    new_slab->free_cnt--;
    return new_slab->obj_area;
}

//------------------------页级释放：负责释放连续的 n 页并将它们回收到空闲页链表中，同时尽量合并相邻空闲页以减少内存碎片。//
static void
default_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;

    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page* page = le2page(le, page_link);
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }
        }
    }

    list_entry_t* le = list_prev(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (p + p->property == base) {
            p->property += base->property;
            ClearPageProperty(base);
            list_del(&(base->page_link));
            base = p;
        }
    }

    le = list_next(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (base + base->property == p) {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
        }
    }
}

static void free_obj(void* object)
{
    //首先应根据object的地址找到其所属的slab
    for(size_t i=0;i<cache_n;i++)
    {
        kmem_cache *cache=&kmalloc_caches[i];
        list_entry_t *le=&cache->slab;
        while((le=list_next(le))!=&cache->slab)
        {
            slab_t *slab=le2slab(le,slab_node);
            //判断object是否在该slab的object空间中
            if((object>=slab->obj_area)&&(object<(slab->obj_area+cache->object_size*cache->object_num)))
            {
                //index= obj 相对于对象区起始的字节偏移/object大小
                size_t index=((char*)object-(char*)slab->obj_area)/cache->object_size;
                size_t byte=index/8;
                size_t bit=index%8;
                if (slab->bitmap[byte] & (1 << bit))
                {
                    slab->bitmap[byte] &= ~(1 << bit); // 标记为未分配
                    slab->free_cnt++;
                    memset(object,0,cache->object_size);
                    if(slab->free_cnt==cache->object_num)
                    {
                        list_del(&slab->slab_node);
                        //PADDR(slab)：把 slab 的虚拟地址转换成物理地址
                        //pa2page(...)：把物理地址转换为 struct Page*（页结构）——用于页分配器接口。
                        default_free_pages(pa2page(PADDR(slab)), 1);
                    }
                }
                return;
            }
        }
    }
}


typedef struct {
    int bit_value;
    slab_t *slab;
    size_t index;
} obj_info_t;
obj_info_t get_obj_info(void *obj) {
    obj_info_t result = { .bit_value = -1, .slab = NULL,.index=-1 };

    for (size_t i = 0; i < cache_n; i++) {
        kmem_cache *cache = &kmalloc_caches[i];
        list_entry_t *le = &cache->slab;

        while ((le = list_next(le)) != &cache->slab) {
            slab_t *slab = le2slab(le, slab_node);

            if ((obj >= slab->obj_area) &&
                (obj < slab->obj_area + cache->object_size * cache->object_num)) {
                
                size_t index = ((char *)obj - (char *)slab->obj_area) / cache->object_size;
                size_t byte = index / 8;
                size_t bit = index % 8;

                result.bit_value = (slab->bitmap[byte] >> bit) & 1;
                result.slab = slab;
                result.index=index;
                return result;
            }
        }
    }

    cprintf("Error: object %p does not belong to any known slab!\n", obj);
    return result;
}
static void slub_basic_check(size_t object_size)
{   
    //申请两个相同大小的对象
    //需要分配新的slab
    void *obj1=alloc_obj(object_size);
    //已有slab上有空闲object
    void *obj2=alloc_obj(object_size);
    size_t nr_free_st=nr_free;
    //验证两个对象所在地址不同
    assert(obj1!=obj2);
    //验证位图对应位为1
    obj_info_t info1 = get_obj_info(obj1);
    assert(info1.bit_value==1);
    //验证分配的object地址系统物理内存范围之内
    assert(PADDR(obj1)<=npage * PGSIZE);
    assert(info1.slab->free_cnt==(int)((((PGSIZE-sizeof(slab_t))/(object_size+1.0/8.0))-2)));
    cprintf("——————————————2个%lu 字节对象分配测试成功————————————\n", object_size);
    //释放
    free_obj(obj1);
    //有占用对象时不释放slab
    assert(nr_free_st==nr_free);
    //测试释放的object正确链入slab
    assert(info1.slab->free_cnt==(int)((((PGSIZE-sizeof(slab_t))/(object_size+1.0/8.0))-1)));
    ////slab存在时对应位图被正确清0
    assert((info1.slab->bitmap[info1.index / 8] & (1 << (info1.index % 8))) == 0);
    obj_info_t info2 = get_obj_info(obj1);
    assert(info2.bit_value==0);
    free_obj(obj2);
    //对象均空闲时释放slab
    assert(nr_free_st+1==nr_free);
    cprintf("——————————————2个%lu 字节对象释放测试成功————————————\n", object_size);
}
static void slub_fine_grained_check(void) {
    cprintf("——————————————————细粒度交替分配释放测试开始——————————————\n");
    size_t sizes[4] = {30, 60, 120, 250};
    size_t nums[4]={126,63,31,15};
    size_t nr_free_start = nr_free;
    void* objects[1000];
    // 交替分配 25 轮，每轮分配 4 个对象，每种大小一个
    int offset = 0;
    for(int round = 0; round < 25; round++) {
        for(int s = 0; s < 4; s++) {
            void* obj = alloc_obj(sizes[s]);
            assert(obj != NULL);
            objects[offset++] = obj;
            // 检查对象位图被正确标记为1
            obj_info_t info = get_obj_info(obj);
            assert(info.bit_value == 1);
        }
    }
    // 交替释放，每次释放一个对象，按分配顺序
    for(int i = 0; i < offset; i++) {
        void* obj = objects[i];
        obj_info_t info_before = get_obj_info(obj);
        assert(info_before.bit_value == 1);
        // 保存 slab 指针和索引
        slab_t *slab = info_before.slab;
        size_t index = info_before.index;
        free_obj(obj);
        // 如果 slab 还存在，检查 bitmap 对应位已清零
        if (slab->free_cnt != nums[i%4]) {
            assert((slab->bitmap[index / 8] & (1 << (index % 8))) == 0);
        }
    }
    // 检查空闲页恢复
    assert(nr_free == nr_free_start);
    cprintf("——————————————————细粒度交替分配释放测试正确——————————————————\n");
}

static void slub_check(void)
{
    cprintf("——————————————slub内存管理器测试开始——————————————————\n");
    //cprintf("%d\n", (int)(nr_free));
    //-----验证cache的对象数量、验证缓存池kmalloc_cache的初始化都正确-------------------//
    //其中通过打印获取sizeof(slab_t)=40,对应object数量应为(4096-40)/(object_size+1/8)
    size_t object_nums[4]={126,63,31,15};
    for(size_t i=0;i<cache_n;i++)
    {
        assert(kmalloc_caches[i].object_num==object_nums[i]);
    }
    cprintf("——————————————缓冲池初始化正确——————————————————\n");
    //------------基础功能测试：多个同大小的object的分配与释放------//
    //测试alloc_obj()有对应cache时需要分配新的slab+已有slab上有空闲object的情况
    //测试free_obj()中需要释放slab和不需要释放slab的情况
    for(size_t i=32;i<=256;i=i*2)
    {
        slub_basic_check(i);
    }
    //-----------------------测试无空闲页时alloc_slab函数可正确返回NULL------------------
    //暂时清空空闲页链表即无可用页分给slab,验证 alloc_slub() 此时应返回 NULL（即无可用页来分给slab）
   {
    //暂存当前全局空闲页链表和空闲页计数
    list_entry_t free_list_store = free_list;
    size_t nr_free_store=nr_free;
    //将全局空闲页计数清零并初始化一个新的空闲链表
    nr_free = 0;
    list_init(&free_list);
    assert(list_empty(&free_list));
    //测试无空闲物理页时分配 slab应返回 NULL
    size_t a=32;
    size_t b=126;
    assert(alloc_slab(a,b)==NULL);
    //恢复空闲链表和空闲页计数
    free_list = free_list_store;
    nr_free=nr_free_store;
    cprintf("——————————————无空闲物理页时slab分配失败情况测试正确——————————————————\n");
}
    //-------测试object分配边界：请求的对象为非法值：0或超出已有kmem_cache的object_size最大值256
    //测试alloc_slab()中返回NULL的两种情况
    {
        assert(alloc_obj(0)==NULL);
        assert(alloc_obj(512)==NULL);
        cprintf("——————————————object分配边界测试正确——————————————————\n");
    }
    //-------细粒度交替分配释放测试
    slub_fine_grained_check();
    //--------测试大规模非object_size大小的合法object分配与释放
{
    cprintf("————————————————大规模分配释放测试开始————————————————————\n");
    size_t nr_free_start = nr_free;
    size_t nr_free_expect = nr_free;
    void *objects[10000];
    int allocated = 0;
    //交替分配不同缓冲池中的对象
    size_t sizes[4] = {30, 60, 120, 250};
    size_t nums[4]={126,63,31,15};
    int offset = 0;
    for(int s = 0; s < 4; s++) {
        for(int i = 0; i < 1000; i++) {
            void *obj = alloc_obj(sizes[s]);
            objects[offset] = obj;
            offset++;
            allocated++;
        }
        //测试分配后空闲页计数正确
        nr_free_expect-=(1000+nums[s]-1)/nums[s];
        cprintf("分配1000个 %luB 对象后: nr_free = %d (预期 %d)\n",sizes[s], (int)nr_free, (int)nr_free_expect);
        assert(nr_free_expect==nr_free);            
        }   
    assert(nr_free==nr_free_start-(1000+125)/126-(1000+62)/63-(1000+30)/31-(1000+14)/15);
    cprintf("————————————————大规模分配测试正确————————————————————\n");
    // 释放不同缓冲池的对象
    for(int i = 0; i < allocated; i++) {
        free_obj(objects[i]);
    }
    // 测试释放后nr_free恢复
    assert(nr_free == nr_free_start);
    cprintf("————————————————大规模释放测试正确————————————————————\n");
}   
}

static size_t
slub_nr_free_pages(void) {
    return nr_free;
}
//适配层：将 pmm_manager 的页接口映射到本文件的页分配函数
static struct Page *
slub_alloc_pages(size_t n)
{
    return default_alloc_pages(n);
}

static void
slub_free_pages(struct Page *base, size_t n)
{
    default_free_pages(base, n);
}



//实例化内存管理器结构体
const struct pmm_manager slub_pmm_manager = {
    .name = "slub_pmm_manager",
    .init = slub_init,
    .init_memmap = slub_init_memmap,
    .alloc_pages = slub_alloc_pages,   
    .free_pages = slub_free_pages,     
    .nr_free_pages = slub_nr_free_pages,
    .check = slub_check,
};
//对象分配与释放的函数单独提供接口
