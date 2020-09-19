sleep 3s
i="C4"
for j in "07.18"
do
	play "./"$i".wav" &
        ./Runme > "angle_"$j"_"$i"_1500_1m_revisit.txt" &
#	./Runme > "angle_"$j"_"$i"_1500_1m_reflective.txt" &
	sleep 12s &
	wait
done
	
