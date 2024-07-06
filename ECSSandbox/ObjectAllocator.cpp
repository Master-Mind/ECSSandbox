#include "ObjectAllocator.h"
/***************************************************************************/
/*!
  \brief
    Initializes the memory manager
  \param ObjectSize
    The size of the objects that will be allocated
  \param config
    The configuration of the allocator
*/
/***************************************************************************/
ObjectAllocator::ObjectAllocator(size_t ObjectSize, const OAConfig& config) :
    configuration_(config),
    PageList_(nullptr),
    FreeList_(nullptr)
{
    //if the client requests alignment bytes, then provide them
    if (configuration_.Alignment_ != 1 && configuration_.Alignment_)
    {
        //the allignment bytes on the left most sidde of the page
        configuration_.LeftAlignSize_ = (configuration_.Alignment_ -
            //the next page pointer (static_cast required since sizeof returns a size_t,
            //which prevents clean compilation when implicitly converting
            (static_cast<unsigned int>(sizeof(void*)) +
                //the first pad
                configuration_.PadBytes_ +
                //the first header
                static_cast<unsigned int>(configuration_.HBlockInfo_.size_))) %
            configuration_.Alignment_;

        //the allignment bytes inbetween each block
        configuration_.InterAlignSize_ = configuration_.Alignment_ -
            //the size of each object
            (static_cast<unsigned int>(statistics_.ObjectSize_) +
                //the pads inbetween each block
                configuration_.PadBytes_ * 2 +
                //the header between each block
                static_cast<unsigned int>(configuration_.HBlockInfo_.size_)) %
            configuration_.Alignment_;
    }
    //initialize the sizes for statistics, the next page pointer
    statistics_.PageSize_ = sizeof(void*) +
        //the leftmost allignment bytes
        configuration_.LeftAlignSize_ +
        //each object
        (ObjectSize +
            //each set of pads
            configuration_.PadBytes_ * 2 +
            //each header
            configuration_.HBlockInfo_.size_) * configuration_.ObjectsPerPage_ +
        //each intermittent allignment byte set
        configuration_.InterAlignSize_ * (configuration_.ObjectsPerPage_ - 1);
    statistics_.ObjectSize_ = ObjectSize;

    //if pading is requested, then make a pading array to check against
    if (configuration_.PadBytes_ > 0)
    {
        pad_checker = new char[configuration_.PadBytes_];
        std::memset(pad_checker, PAD_PATTERN, configuration_.PadBytes_);
    }

    //allocate the first page
    allocate_new_page();
}

/***************************************************************************/
/*!
  \brief
    Cleans up all of the object allocator's memory
*/
/***************************************************************************/
ObjectAllocator::~ObjectAllocator()
{
    delete[] pad_checker;

    //walk through the page list and delete each page
    while (PageList_)
    {
        char* deletedObject = reinterpret_cast<char*>(PageList_);
        PageList_ = PageList_->Next;
        delete[] deletedObject;
    }
}

/***************************************************************************/
/*!
  \brief
    Clears the header no matter what it is
  \param block
    The block of memory that the header will be cleared from
*/
/***************************************************************************/
void ObjectAllocator::clearHeader(char* block)
{
    switch (configuration_.HBlockInfo_.type_)
    {
        //if there is no header, then nothing is cleared
    case OAConfig::hbNone:
        return;
        //extended reuses some logic from basic blocks
    case OAConfig::hbExtended:
        //set the bytes of the block back to zero except for the use counter
        //the block address needs to be moved to the correct position
        std::memset(block -
            configuration_.PadBytes_ -
            configuration_.HBlockInfo_.size_,
            0,
            //only set the bytes up to the use counter to zero
            configuration_.HBlockInfo_.size_ -
            sizeof(int) -
            sizeof(char) -
            sizeof(short));
        //set everything in the basic header to zero
    case OAConfig::hbBasic:
        std::memset(block -
            sizeof(int) -
            sizeof(char) -
            configuration_.PadBytes_,
            0,
            sizeof(int) +
            sizeof(char));
        return;
        //the external header block
    case OAConfig::hbExternal:
        //check to see if the block's label exists, and if it does, delete it
        if ((*reinterpret_cast<MemBlockInfo**>(block -
            configuration_.PadBytes_ -
            configuration_.HBlockInfo_.size_))->label)
        {
            delete[](
                *reinterpret_cast<MemBlockInfo**>(block -
                    configuration_.PadBytes_ -
                    configuration_.HBlockInfo_.size_))->
                label;
        }

        //delete the block
        delete* reinterpret_cast<MemBlockInfo**>(block -
            configuration_.PadBytes_ -
            configuration_.HBlockInfo_.size_);
        //set its pointer to null to signify that the block is free
        *reinterpret_cast<MemBlockInfo**>(block -
            configuration_.PadBytes_ -
            configuration_.HBlockInfo_.size_) =
            nullptr;
        return;
    }
}

/***************************************************************************/
/*!
  \brief
    Creates a new page and adds its blocks to the free list
*/
/***************************************************************************/
void ObjectAllocator::allocate_new_page(void)
{
    //the page being created
    GenericObject* newPage = nullptr;

    //make sure that there is memory
    try
    {
        //alocate the page and convert it to a generic object
        newPage =
            reinterpret_cast<GenericObject*>(new char[statistics_.PageSize_]);
        if (PageList_ == nullptr)
        {
            PageList_ = newPage;
        }
    }
    catch (std::bad_alloc)
    {
        //throw a custom exception if new failed
        throw OAException(OAException::E_NO_MEMORY,
            "The computer is out of physical memory");
    }

    //if debug is activated, then put the unallocated pattern in
    if (configuration_.DebugOn_)
    {
        std::memset(newPage, UNALLOCATED_PATTERN, statistics_.PageSize_);
        //put in the allignment pattern
        if (configuration_.Alignment_ != 1 && configuration_.Alignment_)
        {
            std::memset(reinterpret_cast<char*>(newPage) +
                sizeof(void*),
                ALIGN_PATTERN,
                configuration_.LeftAlignSize_);
        }
    }

    //set the first pointer
    newPage->Next = nullptr;
    //if the page list exists, then push this page to the end of it
    if (!firstPage_)
    {
        newPage->Next = PageList_;
        PageList_ = newPage;
    }
    else
    {
        firstPage_ = false;
    }
    //put the blocks on the free block list
    for (unsigned int i = 0; i < configuration_.ObjectsPerPage_; i++)
    {
        reinterpret_cast<GenericObject*>(AccessBlock(i))->Next = nullptr;
        //while we are at the block, put any requested padding bytes
        if (configuration_.PadBytes_)
        {
            std::memset(AccessBlock(i) -
                configuration_.PadBytes_,
                PAD_PATTERN,
                configuration_.PadBytes_);

            std::memset(AccessBlock(i) +
                statistics_.ObjectSize_,
                PAD_PATTERN,
                configuration_.PadBytes_);
        }
        //if a header is requested, the initialize the headers to zero
        if (configuration_.HBlockInfo_.type_ != OAConfig::hbNone)
        {
            std::memset(AccessBlock(i) -
                configuration_.HBlockInfo_.size_ -
                configuration_.PadBytes_,
                0,
                configuration_.HBlockInfo_.size_);
        }
        //set the allignment pattern for intermittent bytes
        if (configuration_.DebugOn_ &&
            configuration_.Alignment_ != 1 &&
            configuration_.Alignment_ && i != 0)
        {
            std::memset(AccessBlock(i) -
                configuration_.HBlockInfo_.size_ -
                configuration_.PadBytes_ -
                configuration_.InterAlignSize_,
                ALIGN_PATTERN,
                configuration_.InterAlignSize_);
        }

        //push this block onto the free list
        FLPush(reinterpret_cast<GenericObject*>(AccessBlock(i)));
    }


    //track the number of pages
    ++statistics_.PagesInUse_;
}

/***************************************************************************/
/*!
  \brief
    Pushes a block onto the free list
  \param block
    The block that was freed
*/
/***************************************************************************/
void ObjectAllocator::FLPush(GenericObject* block)
{
    block->Next = FreeList_;
    FreeList_ = block;
    ++statistics_.FreeObjects_;
}

/***************************************************************************/
/*!
  \brief
    Takes the first item off of the free list
*/
/***************************************************************************/
void ObjectAllocator::FLPop()
{
    FreeList_ = FreeList_->Next;
    --statistics_.FreeObjects_;
}

/***************************************************************************/
/*!
  \brief
    Initializes a block header
  \param block
    The block whose header will be initialized
  \param incrementAlloced
    Whether or not to increment the alloced feild of the block
  \param setAlloced
    Whether or not to set the alloced feild of the block
  \param incrementUse
    Whether or not to increment the use feild of the block
  \param label
    The label to put on the external header block
*/
/***************************************************************************/
void ObjectAllocator::SetHeader(void* block,
    bool incrementAlloced,
    bool setAlloced,
    bool incrementUse,
    const char* label)
{
    //do nothing if there are no headers
    if (configuration_.HBlockInfo_.type_ == OAConfig::hbNone)
    {
        return;
    }
    else if (configuration_.HBlockInfo_.type_ == OAConfig::hbBasic)
    {
        //set the allocation counter
        if (incrementAlloced)
        {
            *reinterpret_cast<unsigned int*>((reinterpret_cast<char*>(block) -
                sizeof(int) -
                sizeof(char) -
                configuration_.PadBytes_)) =
                statistics_.Allocations_ + 1;
        }
        //set the allocated bit
        if (setAlloced)
        {
            *(reinterpret_cast<char*>(block) -
                sizeof(char) -
                configuration_.PadBytes_) |=
                1;
        }
    }
    else if (configuration_.HBlockInfo_.type_ == OAConfig::hbExtended)
    {
        //set the allocation counter
        if (incrementAlloced)
        {
            *reinterpret_cast<unsigned int*>((reinterpret_cast<char*>(block) -
                sizeof(int) -
                sizeof(char) -
                configuration_.PadBytes_)) =
                statistics_.Allocations_ + 1;
        }
        //increment the use counter
        if (incrementUse)
        {
            ++*reinterpret_cast<int*>((reinterpret_cast<char*>(block) -
                sizeof(int) -
                sizeof(char) -
                sizeof(short) -
                configuration_.PadBytes_));
        }
        //set the allocated bit
        if (setAlloced)
        {
            *(reinterpret_cast<char*>(block) -
                sizeof(char) -
                configuration_.PadBytes_) |=
                1;
        }

    }
    else if (configuration_.HBlockInfo_.type_ == OAConfig::hbBasic)
    {
        //set the allocation bit
        if (incrementAlloced)
        {
            ++*reinterpret_cast<int*>((reinterpret_cast<char*>(block) -
                configuration_.HBlockInfo_.size_ -
                configuration_.PadBytes_));
        }
        //set the allocated bit
        if (setAlloced)
        {
            *(reinterpret_cast<char*>(block) -
                sizeof(char) -
                configuration_.PadBytes_) |=
                1;
        }
    }
    else if (configuration_.HBlockInfo_.type_ == OAConfig::hbExternal)
    {
        //set the label
        if (label != nullptr)
        {
            size_t len = std::strlen(label);
            //dynamically allocate a new label
            (*reinterpret_cast<MemBlockInfo**>(reinterpret_cast<char*>(block) -
                configuration_.HBlockInfo_.size_ -
                configuration_.PadBytes_))->label =
                new char[len + 1];

            //copy in the label
            strncpy_s(
                (*reinterpret_cast<MemBlockInfo**>(reinterpret_cast<char*>(block) -
                    configuration_.HBlockInfo_.size_ -
                    configuration_.PadBytes_))->
                label, len,
                label,
                len);
        }
        //increment the use counter
        if (incrementAlloced)
        {
            (*reinterpret_cast<MemBlockInfo**>(reinterpret_cast<char*>(block) -
                configuration_.HBlockInfo_.size_ -
                configuration_.PadBytes_))->
                alloc_num =
                statistics_.Allocations_ + 1;
        }
        //set the allocated bit
        if (setAlloced)
        {
            (*reinterpret_cast<MemBlockInfo**>(reinterpret_cast<char*>(block) -
                configuration_.HBlockInfo_.size_ -
                configuration_.PadBytes_))->in_use =
                true;
        }
    }
}

/***************************************************************************/
/*!
  \brief
    Checks each group of pad bytes for the given block
  \param block
    The block whose pads will be checked
  \return
    Whether there was an underflow, an overflow, or nothing wrong
*/
/***************************************************************************/
ObjectAllocator::PAD_RESULT
ObjectAllocator::CheckPads(char* block) const
{
    //checks to see if the user overwrote before their memory
    if (std::memcmp(block -
        configuration_.PadBytes_,
        pad_checker,
        configuration_.PadBytes_))
    {
        return PRUNDERFLOW;
    }
    //checks to see if the user overwrote after their memory
    else if (std::memcmp(block +
        statistics_.ObjectSize_,
        pad_checker,
        configuration_.PadBytes_))
    {
        return PROVERFLOW;
    }
    //Otherwise, everything should be fine with this block
    else
    {
        return PRFINE;
    }
}

/***************************************************************************/
/*!
  \brief
    Checks each group of pad bytes for the given block, overloaded for ease of
    use in loops
  \param BlockNum
    The block whose pads will be checked
  \return
    Whether there was an underflow, an overflow, or nothing wrong
*/
/***************************************************************************/
ObjectAllocator::PAD_RESULT
ObjectAllocator::CheckPads(unsigned int BlockNum) const
{
    return CheckPads(AccessBlock(BlockNum));
}

/***************************************************************************/
/*!
  \brief
    Accesses a specific block in the allocator
  \param BlockNum
    The block whose pads will be checked
  \param startPage
    The page to start looking at, i.e. if blocknum is zero, then the first
    block of start page will be returned;
  \return
    The requested block
*/
/***************************************************************************/
char* ObjectAllocator::AccessBlock(unsigned int BlockNum,
    GenericObject* startPage) const
{
    //the page that the block num refers to
    GenericObject* page = PageList_;

    //if the start page was provided, then start at it instead of the page list
    if (startPage)
    {
        page = startPage;
    }

    //walk through the pages and try to find the correct one
    for (unsigned int pageIndex = 0;
        pageIndex < BlockNum / configuration_.ObjectsPerPage_;
        ++pageIndex)
    {
        if (page != nullptr)
        {
            page = page->Next;
        }
        //if the block could not be found, then return null
        else
        {
            return nullptr;
        }
    }

    //                    V-- the pointer at the start of the page
    return &reinterpret_cast<char*>(page)[sizeof(void*) +
        configuration_.LeftAlignSize_ +
        //V-- the frontal pad bytes
        configuration_.PadBytes_ *
        (BlockNum % configuration_.ObjectsPerPage_ + 1) +
        //V-- the end pad bytes
        (BlockNum % configuration_.ObjectsPerPage_) *
        configuration_.PadBytes_ +
        //V-- the header blocks
        (BlockNum % configuration_.ObjectsPerPage_ + 1) *
        configuration_.HBlockInfo_.size_ +
        //V-- the objects
        (BlockNum % configuration_.ObjectsPerPage_) *
        statistics_.ObjectSize_ +
        //V-- the intermittent allignment bytes
        (configuration_.InterAlignSize_) *
        (BlockNum % configuration_.ObjectsPerPage_)];
}

/***************************************************************************/
/*!
  \brief
    Checks to see if the given block is allocated,
    WARNING: if no headers are used, then this will be in O(N) due to the need
    to search through the free list
  \param block
    The block to be checked
  \return
    Whether the block is allocated
*/
/***************************************************************************/
bool ObjectAllocator::IsBlockAllocated(char* block) const
{
    //the current node in the object list
    GenericObject* current = FreeList_;
    //if there is no header, then the only way to check if a block is free is to 
    //see if it is on the free list
    switch (configuration_.HBlockInfo_.type_)
    {
    case OAConfig::HBLOCK_TYPE::hbNone:
        //walk through the list and try to find the block given
        while (current)
        {
            if (reinterpret_cast<char*>(current) == block)
            {
                return false;
            }
            current = current->Next;
        }

        return true;
        //basic and extended use the same logic, just check the allocated bit
    case OAConfig::HBLOCK_TYPE::hbBasic:
    case OAConfig::HBLOCK_TYPE::hbExtended:
        return *(block - sizeof(char) - configuration_.PadBytes_) & 1;
        //if the external header was not allocated, then the block is unallocated
    case OAConfig::HBLOCK_TYPE::hbExternal:
        return *reinterpret_cast<MemBlockInfo**>(block -
            sizeof(void*) -
            configuration_.PadBytes_) !=
            nullptr;
    }

    //the block was not found
    return false;
}

/***************************************************************************/
/*!
  \brief
    Removes all of the free list nodes pointing into the given page
  \param page
    The page whose free nodes will be removed
*/
/***************************************************************************/
void ObjectAllocator::FreeListRemove(char* page)
{
    //the current node in the list
    GenericObject* current = FreeList_;
    //the previous node
    GenericObject* previous = nullptr;

    //walk through the list and try to find blocks in the page given
    while (current)
    {
        //if the current node is in the given page
        if (reinterpret_cast<char*>(current) > page &&
            reinterpret_cast<char*>(current) < page + statistics_.PageSize_)
        {
            //if the previous node is null, then that means that the free list points
            //into the given page, and must be moved forward
            if (previous)
            {
                previous->Next = current->Next;
            }
            else
            {
                FreeList_ = FreeList_->Next;
            }
            --statistics_.FreeObjects_;
        }

        //increment pointers
        previous = current;
        current = current->Next;
    }
}

/***************************************************************************/
/*!
  \brief
    Checks to see if the given pointer is on a good address boundry
  \param block
    The block whose address will be checked
  \return
    Whether the address is on a good boundry (true == good)
*/
/***************************************************************************/
bool ObjectAllocator::CheckBoundry(void* const block) const
{
    //the page that is currently being checked
    GenericObject* curPage = PageList_;
    //the size of the first set of bytes before the first block
    //                     V-- the next page pointer
    size_t startingPoint = sizeof(GenericObject*) +
        //                     V-- The first pad
        configuration_.PadBytes_ +
        //                     V-- The first header
        configuration_.HBlockInfo_.size_ +
        //                     V-- The first allignment bytes
        configuration_.LeftAlignSize_;

    //walk through the page list and try to find the page that the block is on
    while (curPage)
    {
        //if the clock is in the page, check to see if it is on a good boundry
        if (block > curPage && block < curPage + statistics_.PageSize_)
        {
            //      V-- the given address
            return (reinterpret_cast<uintptr_t>(block) -
                //      V-- the start of the page it is on
                reinterpret_cast<uintptr_t>(curPage) -
                //      V-- the first few bytes
                startingPoint) %
                //      V-- the size of an object
                (statistics_.ObjectSize_ +
                    //      V-- the pads
                    configuration_.PadBytes_ * 2 +
                    //      V-- the headers
                    configuration_.HBlockInfo_.size_ +
                    //      V-- the allignment bytes
                    configuration_.InterAlignSize_) ==
                //      If that modulas opperation returned zero, then the address is fine
                0;
        }
        curPage = curPage->Next;
    }

    //if the page was not found, then the block is on a bad boundry
    return false;
}

/***************************************************************************/
/*!
  \brief
    Allocates a block and returns a pointer to it
  \param label
    The label to put on the block, only used with external headers
  \return
    A pointer to the allocated block
*/
/***************************************************************************/
void* ObjectAllocator::Allocate(const char* label)
{
    //use new if the clients wants to
    if (configuration_.UseCPPMemManager_)
    {
        try
        {
            //keep track of statistics
            ++statistics_.Allocations_;
            ++statistics_.ObjectsInUse_;
            if (statistics_.ObjectsInUse_ > statistics_.MostObjects_)
            {
                statistics_.MostObjects_ = statistics_.ObjectsInUse_;
            }
            return new char[statistics_.ObjectSize_];
        }
        catch (std::bad_alloc)
        {
            //throw a custom exception if new failed
            throw OAException(OAException::E_NO_MEMORY,
                "The computer is out of physical memory");
        }
    }
    //a pointer to the memory to be allocated
    void* alocMem = reinterpret_cast<void*>(FreeList_);

    //if the memory could be allocated, then remove it from the free list and 
    //set the memory
    if (alocMem)
    {
        FLPop();
        std::memset(alocMem, ALLOCATED_PATTERN, statistics_.ObjectSize_);
        //allocate the header block
        if (configuration_.HBlockInfo_.type_ == OAConfig::hbExternal)
        {
            *reinterpret_cast<MemBlockInfo**>((reinterpret_cast<char*>(alocMem) -
                configuration_.HBlockInfo_.size_ -
                configuration_.PadBytes_)) =
                new MemBlockInfo();
        }
        SetHeader(alocMem, true, true, true, label);
    }
    //otherwise, allocate a new page if doing so is allowed
    else if (configuration_.MaxPages_ == 0 ||
        statistics_.PagesInUse_ < configuration_.MaxPages_)
    {
        allocate_new_page();
        alocMem = reinterpret_cast<void*>(FreeList_);
        FLPop();
        std::memset(alocMem, ALLOCATED_PATTERN, statistics_.ObjectSize_);
        //allocate the header block
        if (configuration_.HBlockInfo_.type_ == OAConfig::hbExternal)
        {
            *reinterpret_cast<MemBlockInfo**>((reinterpret_cast<char*>(alocMem) -
                configuration_.HBlockInfo_.size_ -
                configuration_.PadBytes_)) =
                new MemBlockInfo();
        }
        SetHeader(alocMem, true, true, true, label);
    }
    //otherwise, throw an exception due to the maximum number of pages being 
    //reached
    else if (statistics_.PagesInUse_ == configuration_.MaxPages_)
    {
        throw OAException(OAException::E_NO_PAGES,
            "The maximum number of pages has been reached");
    }

    //keep track of statistics
    ++statistics_.Allocations_;
    ++statistics_.ObjectsInUse_;
    if (statistics_.ObjectsInUse_ > statistics_.MostObjects_)
    {
        statistics_.MostObjects_ = statistics_.ObjectsInUse_;
    }
    return alocMem;
}

/***************************************************************************/
/*!
  \brief
    Frees the given block
  \param Object
    The object to be freed
*/
/***************************************************************************/
void ObjectAllocator::Free(void* Object)
{
    //use new if the clients wants to
    if (configuration_.UseCPPMemManager_)
    {
        --statistics_.ObjectsInUse_;
        ++statistics_.Deallocations_;
        delete[] reinterpret_cast<char*>(Object);
        return;
    }
    //check to see if the use is double deleting
    if (configuration_.DebugOn_ &&
        !IsBlockAllocated(reinterpret_cast<char*>(Object)))
    {
        throw OAException(OAException::E_MULTIPLE_FREE,
            "The object you were attempting to free was already freed");
    }
    //check to see if the address is on a bad boundry
    else if (configuration_.DebugOn_ && !CheckBoundry(Object))
    {
        throw OAException(OAException::E_BAD_BOUNDARY,
            "The pointer that was given to free is on a bad boundry");
    }
    //check to see if the pad bytes are corrupted
    PAD_RESULT result = CheckPads(reinterpret_cast<char*>(Object));
    if (configuration_.DebugOn_ && result != PRFINE)
    {
        throw OAException(OAException::E_CORRUPTED_BLOCK,
            "The Block was determined to be corrupted while freeing");
    }
    //simply push the object onto the free list
    std::memset(Object, FREED_PATTERN, statistics_.ObjectSize_);

    clearHeader(reinterpret_cast<char*>(Object));
    FLPush(reinterpret_cast<GenericObject*>(Object));
    --statistics_.ObjectsInUse_;
    ++statistics_.Deallocations_;
}

/***************************************************************************/
/*!
  \brief
    Calls the given function on each block that is allocated
  \param fn
   The dumping function
  \return
    The number of blocks that are allocated
*/
/***************************************************************************/
unsigned ObjectAllocator::DumpMemoryInUse(DUMPCALLBACK fn) const
{
    //walk through the pages and use the callback on allocated pages
    for (unsigned int i = 0;
        i < statistics_.PagesInUse_ * configuration_.ObjectsPerPage_;
        ++i)
    {
        if (IsBlockAllocated(AccessBlock(i)))
        {
            fn(AccessBlock(i), statistics_.ObjectSize_);
        }
    }

    return statistics_.ObjectsInUse_;
}

/***************************************************************************/
/*!
  \brief
    Calls the given function on each block that is potentially corrupted
  \param fn
   The validation function
  \return
    The number of blocks that are potentially corrupted
*/
/***************************************************************************/
unsigned ObjectAllocator::ValidatePages(VALIDATECALLBACK fn) const
{
    unsigned int corrupted = 0;
    //walk through the pages and use the callback on any potentially corrupted 
    //blocks
    for (unsigned int i = 0;
        i < statistics_.PagesInUse_ * configuration_.ObjectsPerPage_;
        ++i)
    {
        char* block = AccessBlock(i);
        //check the padding bytes for each block
        PAD_RESULT check = CheckPads(block);

        if (check == PROVERFLOW || check == PRUNDERFLOW)
        {
            fn(block, statistics_.ObjectSize_);
            ++corrupted;
        }
    }

    return corrupted;
}

/***************************************************************************/
/*!
  \brief
    Frees pages that are empty
  \return
    The number of pages that were freed
*/
/***************************************************************************/
unsigned ObjectAllocator::FreeEmptyPages(void)
{
    //the current page being checked
    GenericObject* curPage = PageList_;
    //the pointer to the page whose next pointer needs to be reset
    GenericObject** nextPointer = &PageList_;
    //whether or not the page is empty
    bool PageIsEmpty;
    //the number of empty pages
    int emptyPages = 0;

    //walk through the page list and find empty pages
    while (curPage)
    {
        //reset page is empty
        PageIsEmpty = true;
        //walk through the page and check to see if all of the blocks are allocated
        for (unsigned int i = 0; i < configuration_.ObjectsPerPage_; ++i)
        {
            if (IsBlockAllocated(AccessBlock(i, curPage)))
            {
                PageIsEmpty = false;
                break;
            }
        }

        //if the page is empty, then remove it from the list and delete it
        if (PageIsEmpty)
        {
            *nextPointer = curPage->Next;
            FreeListRemove(reinterpret_cast<char*>(curPage));
            --statistics_.PagesInUse_;
            if (curPage == PageList_)
            {
                PageList_ = PageList_->Next;
            }
            delete[] reinterpret_cast<char*>(curPage);
            curPage = *nextPointer;
            ++emptyPages;
        }
        //otherwise, carry on
        else
        {
            nextPointer = &curPage->Next;
            curPage = curPage->Next;
        }
    }

    if (statistics_.PagesInUse_ == 0)
    {
        PageList_ = nullptr;
    }

    return 0;
}

/***************************************************************************/
/*!
  \brief
    Sets the debug state
  \param State
    Whether debug is on or not
*/
/***************************************************************************/
void ObjectAllocator::SetDebugState(bool State)
{
    configuration_.DebugOn_ = State;
}

/***************************************************************************/
/*!
  \brief
    Returns the free list
  \return
    The free list
*/
/***************************************************************************/
const void* ObjectAllocator::GetFreeList(void) const
{
    return FreeList_;
}

/***************************************************************************/
/*!
  \brief
    Returns the page list
  \return
    The page list
*/
/***************************************************************************/
const void* ObjectAllocator::GetPageList(void) const
{
    return PageList_;
}

/***************************************************************************/
/*!
  \brief
    Returns the configuration struct
  \return
    The config struct
*/
/***************************************************************************/
OAConfig ObjectAllocator::GetConfig(void) const
{
    return configuration_;
}

/***************************************************************************/
/*!
  \brief
    Returns the statistics struct
  \return
    The config struct
*/
/***************************************************************************/
OAStats ObjectAllocator::GetStats(void) const
{
    return statistics_;
}
