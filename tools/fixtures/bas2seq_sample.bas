labelSetup:
REM this comment is deliberately long so that wrapping has to break it on a space and the whole line is past seventy nine
TP$ = "AAAAAAAAAAAAAAAAAAAA" + "AAAAAAAAAAAAAAAAAAAA" + "AAAAAAAAAAAAAAAAAAAA" + "AAAAAAAAAAAAAAAAAAAA" + "AAAAAAAAAAAAAAAAAAAA" + "AAAAAAAAAAAAAAAAAAAA" + "AAAAAAAAAAAAAAAAAAAA" + "AAAAAAAAAAAAAAAAAAAA" + "XXXXXXX"
GO TO labelSetup
labelStartSelRoll:
PRINT "HI"
labelChooseDrive:
GOTO labelChooseDriveKP
labelChooseDriveKP:
PRINT "BYE":                                                                        REM trailing comment that makes the line long
