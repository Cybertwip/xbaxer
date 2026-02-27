@echo off
setlocal EnableDelayedExpansion
title Turn-Based PONG

:: Initialize variables
set /a ballX=20, ballY=10
set /a dirX=1, dirY=1
set /a paddle=9
set /a score=0

:loop
:: Update ball position
set /a ballX+=dirX
set /a ballY+=dirY

:: Bounce off top and bottom walls
if !ballY! leq 1 set /a dirY=1
if !ballY! geq 18 set /a dirY=-1

:: Bounce off right wall
if !ballX! geq 38 set /a dirX=-1

:: Paddle collision (Left side)
if !ballX! leq 2 (
    :: Check if ball hits anywhere on the 4-character high paddle
    set /a maxP=paddle+3
    if !ballY! geq !paddle! if !ballY! leq !maxP! (
        set /a dirX=1
        set /a score+=1
    ) else (
        goto gameover
    )
)

:: Draw screen frame
cls
echo Score: !score!
echo ======================================
for /L %%Y in (1,1,18) do (
    set "line="
    for /L %%X in (1,1,38) do (
        set "char= "
        
        :: Draw Paddle
        if %%X==1 (
            set /a pEnd=paddle+3
            if %%Y geq !paddle! if %%Y leq !pEnd! set "char=]"
        )
        :: Draw Ball
        if %%X==!ballX! if %%Y==!ballY! set "char=O"
        
        :: Draw Right Wall
        if %%X==38 set "char=|"
        
        set "line=!line!!char!"
    )
    echo(!line!
)
echo ======================================
echo Controls: [W]+Enter = Up ^| [S]+Enter = Down ^| [Enter] = Wait

:: Get Input (Native CMD fallback since 'choice' doesn't exist)
set "move="
set /p "move=> "

if /I "!move!"=="w" set /a paddle-=2
if /I "!move!"=="s" set /a paddle+=2

:: Keep paddle within screen bounds
if !paddle! leq 1 set /a paddle=1
if !paddle! geq 15 set /a paddle=15

goto loop

:gameover
cls
echo ======================================
echo.
echo              GAME OVER!
echo            Your Score: !score!
echo.
echo ======================================
echo Press Enter to exit...
set /p "dummy="
exit /b