// FighterServer.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include "pch.h"
#include <iostream>
#include "FightServer.h"

FightServer gFightServer;

int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    timeBeginPeriod(1); // 타이머 해상도 높이기

    if (gFightServer.Initialize() == false)
        return EXIT_FAILURE;

    if (gFightServer.ServerProcess() == false)
        return EXIT_FAILURE;

    timeEndPeriod(1);
    return EXIT_SUCCESS;
}
