@echo off
setlocal EnableDelayedExpansion

:: Initialize variables
set /a ballX=20, ballY=10
set /a dirX=1, dirY=1
set /a paddle=9
set /a score=0
set /a loopCount=0
set /a speed=5

:loop
set /a loopCount+=1

:: Only update physics every X loops to simulate "Real Time" speed
set /a tick=loopCount %% speed
if !tick! NEQ 0 goto draw

:physics
set /a ballX+=dirX
set /a ballY+=dirY

:: Bounce off top and bottom walls
if !ballY! leq 1 set /a dirY=1
if !ballY! geq 18 set /a dirY=-1

:: Bounce off right wall
if !ballX! geq 38 set /a dirX=-1

:: AI Paddle Movement (Since we can't read keys without pausing)
if !ballY! gtr !paddle! set /a paddle+=1
if !ballY! lss !paddle! set /a paddle-=1

:: Paddle collision (Left side)
if !ballX! leq 2 (
    set /a pEnd=paddle+3
    if !ballY! geq !paddle! if !ballY! leq !pEnd! (
        set /a dirX=1
        set /a score+=1
    ) else (
        goto gameover
    )
)

:draw
:: Clear screen using the ANSI-supported CLS
cls
echo Score: !score!  ^|  Mode: Real-Time Auto-Paddle
echo ======================================
for /L %%Y in (1,1,18) do (
    set "line="
    for /L %%X in (1,1,38) do (
        set "char= "
        if %%X==1 (
            set /a pEnd=paddle+3
            if %%Y geq !paddle! if %%Y leq !pEnd! set "char=]"
        )
        if %%X==!ballX! if %%Y==!ballY! set "char=O"
        if %%X==38 set "char=|"
        set "line=!line!!char!"
    )
    echo(!line!
)
echo ======================================
echo [Terminal is rendering at max FPS]

:: Small internal delay to prevent Xbox CPU spike
for /L %%i in (1,1,100) do (set dummy=%%i)

goto loop

:gameover
cls
echo ======================================
echo              GAME OVER!
echo            Your Score: !score!
echo ======================================
echo Press Enter to restart...
set /p "restart="
goto loop