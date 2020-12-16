// TCPServe.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "OverLapppedIO.h"
using namespace std;

int main()
{
    std::cout << "服务端\n";

    COverLapppedIO::GetInstance().Init();

    int i = 0;
    while (1)
    {
        cout << "\n -> :";
        cin >> i;

        switch (i)
        {
            case 1: {
                COverLapppedIO::GetInstance().SendData("9111111121232564111111111111111111111111111199888666", AppType::AppType_CloudDesktop, Cmd::Cmd_GetMicroCode);
            }break;

            case 2: {

            }break;

            default:
                break;
        }

        if (i == 3)
        {
            break;
        }
    }

    COverLapppedIO::GetInstance().Uninit();

    std::cout << "Hello World!\n";
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件
